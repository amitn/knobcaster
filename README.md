# ESP32-S3 Cast Knob

A physical volume/transport controller for **Google Cast (Chromecast) speakers**,
built on the [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8).

Turn the knob to change volume. Push to play/pause. Tap the round touchscreen to
jump between every Cast speaker on your Wi-Fi, see what's playing, and control
transport (play / pause / stop / next / previous).

```
        ╭───────────────╮
        │   Kitchen     │   ← Cast device name
        │  ▶  Bad Guy   │   ← now playing (title / artist)
        │   Billie...   │
        │   ▮▮▮▮▮▯▯  62% │   ← volume ring follows the knob
        ╰───────────────╯
   dial = volume · swipe = change speaker · press = play/pause
```

## Status

🚧 **Design / bring-up phase.** This repo currently contains the implementation
docs and build tooling. Firmware sources land per the [roadmap](docs/06-roadmap.md).

## Quick start

Prerequisites: [`uv`](https://docs.astral.sh/uv/) and [`just`](https://github.com/casey/just).
PlatformIO itself is installed *into a uv-managed venv* — you do **not** need a
global PlatformIO.

```bash
just setup          # create .venv via uv, install pinned PlatformIO into it
cp include/secrets.h.example include/secrets.h   # add your Wi-Fi creds
just flash          # build + upload to the board
just monitor        # watch serial logs
```

Run `just` with no arguments to list every recipe.

## How it works (one paragraph)

The ESP32-S3 joins your Wi-Fi, then uses **mDNS** to discover `_googlecast._tcp`
services on the LAN. For a selected speaker it opens a **TLS** socket to port
`8009` and speaks the **CASTV2** protocol (length-prefixed protobuf frames whose
payloads are JSON) to read media/volume status and send transport commands. The
knob, touchscreen, and a small **LVGL** GUI form the control surface. See
[docs/02-architecture.md](docs/02-architecture.md).

## Documentation

| Doc | What's in it |
|-----|--------------|
| [00 — Overview & goals](docs/00-overview.md) | Scope, MVP, non-goals |
| [01 — Hardware reference](docs/01-hardware.md) | Board, ICs, GPIO pin map |
| [02 — Firmware architecture](docs/02-architecture.md) | Tasks, modules, data flow |
| [03 — Cast protocol](docs/03-cast-protocol.md) | mDNS, CASTV2, namespaces, message flows |
| [04 — UI / UX](docs/04-ui-ux.md) | Knob + touch interaction model, LVGL screens |
| [05 — Build & tooling](docs/05-build-and-tooling.md) | uv + Just + PlatformIO workflow |
| [06 — Roadmap](docs/06-roadmap.md) | Milestones & open questions |

## Toolchain

- **[uv](https://docs.astral.sh/uv/)** — owns the Python venv that PlatformIO runs in.
- **[just](https://github.com/casey/just)** — task runner (`just setup`, `just flash`, …).
- **[PlatformIO](https://platformio.org/)** — build/flash toolchain (**ESP-IDF** framework).
- **[ESP-IDF](https://docs.espressif.com/projects/esp-idf/)** — `esp-tls`, `mdns`, `cJSON`, drivers.
- **[LVGL 9](https://lvgl.io/)** — embedded GUI (via `esp_lvgl_port`).

## Prior art / references

We don't start from scratch — these inform the design (see
[docs/02-architecture.md](docs/02-architecture.md#prior-art--what-we-borrow)):

- **[ESPCaster](https://github.com/amitn/ESPCaster)** — same author's ESP-IDF
  Chromecast discovery + control + LVGL stack (on a different board). Primary
  reference for the Cast layer.
- **[EmbeddedWizard BSP](https://github.com/EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN)**
  — ESP-IDF BSP for *this exact board*; source of the confirmed pin map.
- **[BlueKnob](https://github.com/joshuacant/BlueKnob)** — BLE media remote on
  this board; source of the **SH8601** display driver + BSP components (touch,
  backlight PWM, encoder).
- **[roon-knob](https://github.com/muness/roon-knob)** — Roon controller on this
  board (close analog: media transport + volume over the network).
- **[ihayri dev-board examples](https://github.com/ihayri/ESP32-S3-1.8inch-Knob-Display-Development-Board)**
  and **[VolosR/Knob18Meters](https://github.com/VolosR/Knob18Meters)** — more
  display/UI examples for this exact board.
