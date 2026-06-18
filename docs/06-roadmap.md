# 06 — Roadmap & Open Questions

Milestones are ordered to de-risk the unknowns early (hardware bring-up and the
Cast protocol) before polishing UX.

## M0 — Tooling & skeleton ✅ (this commit)
- [x] uv + Just + PlatformIO (ESP-IDF) scaffolding
- [x] `sdkconfig.defaults`, `partitions.csv`, `src/idf_component.yml`
- [x] Implementation docs (this set)
- [x] Pin map confirmed against EmbeddedWizard BSP ([01](01-hardware.md))
- [x] `src/app_main.c` boot + heap/wifi heartbeat log + `src/CMakeLists.txt`
- [x] Custom board JSON (`boards/esp32s3-knob.json`) — **build passes** (RAM 11%, Flash 11%)
- [x] `include/secrets.h.example` (now optional — see M6 Wi-Fi provisioning)

## M1 — Hardware bring-up
- [x] All pins confirmed incl. push-button **GPIO0** (EmbeddedWizard `ew_bsp_inout.c`)
- [x] Display controller identified: **SH8601** (not ST77916) — both BSPs agree
- [x] SH8601 QSPI panel + backlight up (`components/bsp/display.c`, 185 init cmds) — built
- [x] LVGL 9 via `esp_lvgl_port`; now-playing screen w/ volume arc (`components/ui`) — built
- [x] app_main drives the screen from the Cast session (`ui_set_now_playing`)
- [x] CST816 touch (`esp_lcd_touch_cst816s`, IDF6 I2C-master) → LVGL pointer (`touch.c`) — built
- [x] Encoder via PCNT (x4) + button GPIO0 ISR → `knob_take_delta/pressed` (`knob.c`) — built

> Display + UI build clean; **untested on hardware** — panel init, color/byte
> order, and backlight polarity need on-device validation.

## M2 — Network & discovery
- [x] Wi-Fi connect from `secrets.h`, reconnect/backoff (`components/wifi`)
- [x] mDNS browse `_googlecast._tcp`; parse TXT (`fn`,`md`,`id`) → `cast_device_t[]`
      (`components/cast/cast_discovery.c`); logs device list each scan
- [ ] Render discovered device list on screen (needs M1 display)

## M3 — Cast read path  (reference: ESPCaster `chromecast_controller`)
- [x] TLS connect to `:8009` via `esp-tls` (`skip_common_name`) — `cast_connection.c`
- [x] CASTV2 framing + minimal `CastMessage` protobuf encode/decode — `cast_proto.c`
- [x] Handshake: CONNECT + GET_STATUS (`cast_session_open`)
- [x] PING keepalive (5s) + PONG reply to device PINGs (`cast_session_poll`)
- [x] Parse RECEIVER_STATUS → volume + app transportId (cJSON) → media transport
- [x] Media CONNECT + GET_STATUS → now-playing (title/artist) + mediaSessionId
      + supportedMediaCommands (`cast_session.c`)
- [x] Live status pushed into session snapshots; `run_session` logs now-playing
- [ ] Reduce into a shared `AppState` for the UI (with M5)

> Builds clean (managed `espressif/cjson`); **untested on a real Cast device** —
> TLS handshake, framing, and payload shapes need on-device validation.

## M4 — Cast control path
- [x] Command API in `cast_session`: set/step volume, mute, play, pause,
      toggle, stop, next, prev (QUEUE_UPDATE jump) — built, not device-tested
- [x] Wire SET_VOLUME to knob rotation (optimistic; arc follows) — `run_session`
- [x] Wire Play/Pause to knob press — `run_session`
- [x] Volume debounce (~16 Hz flush of latest target) + reconcile (accept device
      level only when no local change in flight) — `cast_session_poll`
- [x] Mute via knob **long-press** (~600 ms) — `knob_take_long_pressed` + `set_muted`
- [x] On-screen transport buttons (prev / play-pause / next) wired to the session;
      center play/pause icon follows player state (`ui_*transport`, `ui_set_playing`)
- [x] Supported-commands gating in the UI (dim next/prev/play when unsupported)
      — `ui_set_transport_enabled` fed by `supports_prev/pause/next` (`app_main` →
      `set_btn_enabled` dims the buttons)

## M5 — Multi-device UX
- [x] Switch active device via **swipe left/right** (LVGL gesture → `ui_take_swipe`;
      `run_session` reopens the new device without rescanning) — built
- [x] Sticky selection: keep the chosen device active across rescans (match by id)
- [x] Device-list overlay (tap center): modal list of all speakers with cached
      per-device state (`g_cache`); knob/tap to pick, bg-tap/12s to dismiss
      (`device_list_overlay` + `ui_devlist_*`) — built
- [x] Warm-session pool (3 LRU sessions) for instant switch-back — built
      (`g_pool` in `app_main.c`; periodic mDNS every 30s, sessions survive rescans)
- [x] Cast **group** handling — group volume on the knob (receiver), plus a
      members overlay (multizone: `MULTIZONE_STATUS`/`DEVICE_UPDATED`,
      `SET_DEVICE_VOLUME`) where the knob adjusts each member; `ca` bit 0x20
      group detection — built, **needs a real Cast group to validate**
- [x] Album artwork: fetch `media.metadata.images[0].url`, decode JPEG → RGB565
      `lv_image` as a dimmed background. Async on its own task (`components/albumart`,
      esp_http_client + esp_new_jpeg) so it never blocks the UI/net — built

## Testing infra (see [07-testing.md](07-testing.md))
- [x] Design doc: trace capture + native unit tests
- [x] Extract pure `cast_status.{c,h}` parser (rewired `cast_session`) — build green
- [x] `native` env + Unity; `cast_proto` round-trip tests; `just test` (`test/test_proto/`)
- [x] `cast_status` parser tests (`test/test_status/`, 18 cases) — synthetic
      fixtures; cJSON vendored at `lib/cjson/`
- [ ] `cast trace` (`-DCAST_TRACE`) + `just trace`; capture real fixtures on hardware
- [x] CI: `just test` (native tests) + `just build` (compile gate) on every PR/push
      (`.github/workflows/ci.yml`, two parallel jobs; PlatformIO toolchain cached)
- [ ] Swap synthetic fixtures for real captures (needs hardware capture)

## Speaker selection (UX) — done
- [x] Knob **press → device list** (opens instantly), **dial navigates**, press/tap
      **selects**. Dial on now-playing = volume; long-press = mute; play/pause on
      the on-screen transport button. (`run_session` → `device_list_overlay`)

## M6 — Polish & robustness
- [x] Haptics (DRV2605 @ I2C 0x5A): tactile click per encoder detent via a
      worker task (`components/bsp/haptics.c`, shared I2C bus `board_i2c.c`) —
      built, **needs on-device tuning** (ERM vs LRA actuator, effect/library)
- [x] **Screen sleep**: backlight + panel off after 5 min idle, instant wake on
      any input (input swallowed). Cast stays warm, so wake shows live state —
      `display_sleep/wake`, idle timer in `ui_input_task`
- [x] **Per-speaker volume color**: 16-color palette hashed from the speaker name
      (`color_for_name` → `ui_set_volume_color`)
- [x] **Non-Latin titles**: LVGL Tiny-TTF renders an embedded DejaVu subset
      (Latin + Hebrew) with `LV_USE_BIDI` for RTL; `just gen-font` (fonttools)
- [x] **Now-playing title refresh** on track change: re-request media `GET_STATUS`
      when a status push omits the `media` block (`has_media`)
- [~] Error/edge handling: Wi-Fi drop ✓; **unreachable speaker** ✓ (powered
      off / off Wi-Fi → exponential connect backoff 0.5–15 s + "can't reach
      speaker" UI, and device-switch input still honored so you're never stuck on
      a dead speaker — `app_main` net loop). Still TODO: device vanishes from the
      list mid-session, receiver app stops (idle render)
- [ ] Boot time + memory budget pass
- [x] **OTA updates** from GitHub Releases (`components/ota`): checks
      `releases/latest`, compares the tag to the running image's version, pulls
      `firmware.bin` over CA-bundle-verified HTTPS into the inactive slot, reboots
      with rollback protection (`esp_https_ota` + `BOOTLOADER_APP_ROLLBACK_ENABLE`;
      net loop checks ~60s after boot then every 6h). Release CI
      (`.github/workflows/release.yml`) builds + attaches the asset on a `v*` tag,
      stamping the tag into `version.txt`. **Validated on-device** (v0.1.0→v0.1.3
      self-update: download, write inactive slot, reboot, `mark_valid`/rollback,
      "up to date"). Fix needed: GitHub's signed redirect URL overflowed the
      default `esp_http_client` buffers ("Out of buffer") → set
      `buffer_size`/`buffer_size_tx` to 4096 in the OTA client.
- [x] **Web flasher** for the *initial* USB flash (ESP Web Tools / WebSerial):
      `web-flasher/` + `web-flasher.yml` build a merged image and deploy the page
      to GitHub Pages on a `v*` tag (`amitn.github.io/knobcaster`) — needs Pages
      enabled (Settings → Pages → GitHub Actions) and on-device validation
- [x] **Wi-Fi provisioning** — SoftAP + on-LCD QR + web form (`192.168.4.1`) +
      captive-portal DNS (53/udp → 192.168.4.1, so the form auto-opens), creds
      saved to NVS, Wi-Fi status icon (`components/provisioning`, `wifi`,
      `run_provisioning`); compiled `secrets.h` now optional — built

## Open questions

1. ~~Push-button GPIO~~ — **resolved: GPIO0** (EmbeddedWizard `ew_bsp_inout.c`,
   active-low). Encoder A/B = 8/7. Display = SH8601 (not ST77916).
2. **TLS memory footprint** — how many concurrent Cast TLS sessions fit in RAM
   alongside LVGL? Drives the connection strategy (single vs warm-pool).
3. **List previews** — do we want now-playing for *every* device in the list
   (round-robin polling cost) or only the active one for MVP?
4. **Next/Prev portability** — `QUEUE_UPDATE {jump:±1}` vs `QUEUE_NEXT/PREV`
   across receiver apps (Spotify/YT Music/default). Needs device testing.
5. **Groups** — volume math for Cast groups (member vs group level) and whether
   transport even applies.
6. ~~Secondary ESP32~~ — **resolved: it runs stock Bluetooth A2DP/AVRC audio**
   (its CH340 console `1a86:7523` shows a BT boot log). Not a bridge for the S3
   and not unused; we don't program it. Flash only the S3's "USB JTAG/serial
   debug unit" (`303a:1001`). See [01-hardware.md](01-hardware.md#usb--which-chip-is-which-important-for-flashing).
7. **Provisioning** — acceptable to ship compiled-in creds for v1, or is
   BLE/portal provisioning required from the start?

## Risks

| Risk | Mitigation |
|------|------------|
| Cast protocol changes (undocumented) | Pin behavior to pychromecast/node-castv2; isolate in `cast/` |
| Wrong pinout bricks bring-up time | Verify schematic first; M1 gates everything |
| RAM exhaustion (TLS+LVGL+JSON) | PSRAM buffers; single warm session; stream-parse JSON |
| Round display edge clipping | Keep UI inside safe inner circle |
