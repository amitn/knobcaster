# 05 — Build & Tooling

The toolchain is **uv → PlatformIO**, orchestrated by **just**. PlatformIO is a
Python app; we install it into a **uv-managed virtualenv** so the whole firmware
toolchain is pinned and reproducible, with no global PlatformIO required.

```
uv (.venv)  ──owns──▶  platformio  ──drives──▶  espressif32 / ESP-IDF / LVGL
   ▲                                                   │
   └──────────────── just recipes (uv run pio …) ──────┘
```

Framework is **ESP-IDF** (`framework = espidf`), not Arduino — see
[02-architecture.md](02-architecture.md). PlatformIO drives the IDF build; you do
not run `idf.py` directly.

## Prerequisites

- **uv** — <https://docs.astral.sh/uv/> (`curl -LsSf https://astral.sh/uv/install.sh | sh`)
- **just** — <https://github.com/casey/just> (`cargo install just`, `brew install just`, or distro pkg)
- USB access to the board. On Linux add yourself to `dialout` (or `uucp`) for
  serial permissions; under WSL use `usbipd` to attach the USB device.

> No separate Python or PlatformIO install needed — `uv` provides Python and the
> pinned PlatformIO from `pyproject.toml`.

## First-time setup

```bash
just setup        # uv sync → creates .venv, installs PlatformIO into it
```

`just setup` runs `uv sync` (builds `.venv` from `pyproject.toml`) then prints the
PlatformIO version *from inside that venv*. Every other recipe uses `uv run pio …`,
so they can never accidentally hit a system PlatformIO.

## Wi-Fi credentials

**Default: none needed.** On first boot the device provisions over the air — it
opens a `CastKnob-XXXX` SoftAP and shows a QR on the LCD; scan it, open
`192.168.4.1`, and submit your home Wi-Fi. Creds are saved to NVS (see
[02-architecture.md](02-architecture.md#wi-fi-provisioning-softap--qr--web-form)).

For development you can compile creds in instead (skips provisioning):

```bash
cp include/secrets.h.example include/secrets.h
$EDITOR include/secrets.h      # set WIFI_SSID / WIFI_PASS
```

`include/secrets.h` is gitignored. NVS creds take precedence over compiled ones.
To force re-provisioning, erase NVS: `uv run pio run -t erase` (or clear the
saved network).

## Everyday commands

| Command | What it does |
|---------|--------------|
| `just` | list all recipes |
| `just build` | compile firmware |
| `just upload` | flash the board |
| `just flash` | build + upload |
| `just monitor` | serial monitor @ 115200 |
| `just dev` | build + upload + monitor |
| `just ports` | list serial devices |
| `just clean` | remove build artifacts |
| `just check` | static analysis (`pio check`) |
| `just doctor` | print python/pio/platform versions |
| `just distclean` | remove `.pio` **and** `.venv` |

### Selecting the serial port

Auto-detect usually works. To force one:

```bash
just PORT=/dev/ttyACM0 flash
just PORT=/dev/ttyACM0 monitor
```

(`ports` lists candidates. On the dual-MCU board the USB CDC port is bridged via
the secondary ESP32.)

## Why uv owns PlatformIO

- **Reproducible**: PlatformIO version pinned in `pyproject.toml`; `uv.lock`
  captures the full dependency graph.
- **Isolated**: no global pip / no clobbering another project's PlatformIO.
- **Fast & boring**: `uv sync` is quick; CI does the same `just setup` as a dev.

## ESP-IDF configuration files

| File | Role |
|------|------|
| `sdkconfig.defaults` | Seed config: PSRAM octal, 16 MB flash, TLS, USB console. Edit **this**, not the generated `sdkconfig.*`. |
| `partitions.csv` | 16 MB layout: dual OTA apps + SPIFFS + coredump. |
| `src/idf_component.yml` | Managed components pulled from the Espressif registry (LVGL, `esp_lvgl_port`, `esp_lcd_sh8601`, `esp_lcd_touch_cst816s`, `mdns`). |
| `src/CMakeLists.txt` | Registers the `src/` "main" component (added at bring-up). |

To change a config knob interactively: `uv run pio run -t menuconfig`. Persist
anything you want kept into `sdkconfig.defaults`; the generated `sdkconfig.knob`
is gitignored and rebuilt from defaults.

## Board profile note

`platformio.ini` uses `board = esp32s3-knob`, a **custom board JSON** at
`boards/esp32s3-knob.json` (the espressif32 platform ships no N16R8 DevKitC
variant). It declares the ESP32-S3, **16 MB flash**, octal PSRAM (`psram_type:
opi`) and upload params. The actual flash/PSRAM init is driven by
`sdkconfig.defaults` since this is an ESP-IDF build.

## CI sketch (later)

```yaml
# .github/workflows/build.yml (future)
- uses: astral-sh/setup-uv@v3
- run: just setup
- run: just build       # compile-only gate on every PR
```

## Troubleshooting

- **PSRAM not detected** → confirm `board_build.psram_type = opi` and the
  `-DBOARD_HAS_PSRAM` flag; an S3R8 needs *octal*, not quad.
- **Upload can't find port** → `just ports`, then pass `PORT=…`; on WSL re-attach
  with `usbipd`.
- **Display init fails** → almost certainly a pin/driver mismatch; revisit
  [01-hardware.md](01-hardware.md) and verify against the Waveshare demo.
- **`pio` not found** → you ran it directly; always go through `just` / `uv run`.
