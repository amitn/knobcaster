# CLAUDE.md

Working notes for this repo. See `docs/` for depth; this file is the high-signal
summary + gotchas.

## What this is

A Waveshare **ESP32-S3-Knob-Touch-LCD-1.8** desk knob that discovers and controls
**Google Cast / Chromecast** speakers (volume, mute, transport, speaker switching,
groups, album art) over Wi-Fi. ESP-IDF (v6) + LVGL 9, built via PlatformIO inside
a uv-managed venv.

## Build / flash / test

All toolchain runs inside the uv venv — always go through `just` (or `uv run pio`):

```bash
just build          # uv run pio run
just flash          # build + upload to /dev/ttyACM0 (the ESP32-S3 side)
just monitor        # serial monitor (needs an interactive TTY)
just shot [out.png] # capture the LCD framebuffer over serial -> PNG
just test           # host-native unit tests (pure parsers)
just gen-font       # regenerate the embedded Hebrew/Latin TTF subset
```

- **Never `rm -rf` the build.** Use `just clean` / `uv run pio run -t fullclean`.
- **After any Kconfig/`sdkconfig.defaults` change**: `rm -f sdkconfig.knob` then
  `pio run -t fullclean` and rebuild, else stale config sticks.
- Build is sandbox-friendly except the uv cache — run `pio` with the sandbox
  disabled if it hits a read-only `~/.cache/uv` error.

## Board / serial (WSL2)

- The ESP32-S3 native USB enumerates as **`/dev/ttyACM0`** (VID:PID `303a:1001`).
- In WSL it must be attached: `usbipd.exe attach --wsl --busid 2-1`. After a
  host sleep/resume the link goes stale (`vhci_hcd: urb->status -104`, no node) —
  **ask the user to re-plug or re-attach rather than running usbipd yourself.**
- `pio device monitor` needs a real TTY (fails when stdout is redirected). To
  capture logs programmatically, read the port directly with **pyserial** (pulse
  DTR/RTS to reset for boot timing).

## Architecture (firmware)

Tasks, all cooperating through a command queue + mutex-guarded shared state in
`src/app_main.c`:

- **net loop** (`app_main`, prio 1, core 0): mDNS discovery + all Cast TLS. Runs
  a **warm session pool** (`POOL_SIZE` live sessions) so switching back to a
  recent speaker is instant. Periodic discovery (30s); first scan uses a
  `last_discover == 0` sentinel (monotonic `esp_timer` time, so a plain interval
  check would delay the first scan ~30s).
- **`ui_input_task`** (prio 5): reads knob/touch, sends commands, optimistic
  volume arc. Never blocks on the network.
- **encoder** (`components/bsp/knob.c`): interrupt-driven — PCNT watch-points at
  -1/0/+1 fire the `enc_on_reach` ISR (no poll task). Detents decoded on
  departure-from-0; haptics kicked via the ISR-safe `haptics_click_from_isr()`.
- **`albumart` task** (`components/albumart/`, prio 2, core 1): async HTTPS
  fetch + JPEG decode of cover art, off the UI/net threads. **Invariant: art
  fetch/decode must never block the UI or net task.**

Components: `cast/` (discovery, connection, session, pure `cast_status` parsers),
`ui/` (LVGL screens + embedded font), `bsp/` (display/touch/knob), `albumart/`,
`wifi/`, `provisioning/`.

## Gotchas / hard-won lessons

- **FreeRTOS tick is 1 kHz** (`CONFIG_FREERTOS_HZ=1000`). A polling task must
  `vTaskDelay` ≥ 1 tick — at the old 100 Hz, `pdMS_TO_TICKS(4)` truncated to 0
  and the (then poll-based) encoder task busy-spun, starving everything (this was
  the "slow connect" bug). The encoder is now interrupt-driven so it no longer
  polls, but the rule stands for any poll-loop task: sleep ≥ 1 tick and sit below
  the work it feeds in priority.
- **TLS config** (`sdkconfig.defaults`): esp-tls is built in **insecure mode**
  (Cast devices are self-signed) — connect without cert verification, don't
  attach a cert bundle. mbedTLS uses `MBEDTLS_DEFAULT_MEM_ALLOC` so big SSL
  buffers land in PSRAM (warm pool + art fetch otherwise exhaust internal RAM →
  `ssl_setup -0x008D`). **Do NOT enable `MBEDTLS_DYNAMIC_BUFFER`** — it breaks
  large multi-record HTTPS bodies (album art) with `-0x0087`.
- **mbedTLS errors are PSA codes** here: `-0x008D` = -141 = insufficient memory,
  `-0x0087` = -135 = bad input data.
- **Cast media status**: track-change pushes often omit the `media` block, so the
  parser exposes `has_media` and the session re-requests `GET_STATUS` to refresh
  the title (don't poll blindly).
- **Fonts**: default Montserrat is Latin-only. Non-Latin titles render via LVGL
  **Tiny-TTF** with an embedded DejaVu subset (`just gen-font`, Python/fonttools);
  `LV_USE_BIDI` handles RTL. Keep UI symbols on Montserrat.
- **LVGL image cache** is keyed by the `src` pointer — when reusing one image
  descriptor for new pixels, `lv_image_cache_drop()` it before repointing.

## Conventions

- Match surrounding code style (C, ESP-IDF idioms: fixed `char[]`, `cJSON`,
  `strlcpy`, bounded `snprintf`).
- Commit only when asked; end commit messages with the
  `Co-Authored-By: Claude ...` trailer.
