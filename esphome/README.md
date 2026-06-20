# ESPHome port (WIP — branch `esphome-port`)

An experiment in re-targeting this device to **[ESPHome](https://esphome.io)** for
Home Assistant integration + dashboard OTA. See
[../docs/08-esphome-port.md](../docs/08-esphome-port.md) for the feasibility
assessment, component mapping, and phased plan.

- `knob.yaml` — **Phase-1 hardware scaffold** (display, touch, encoder, button,
  backlight, Wi-Fi, OTA). Not yet hardware-validated; the SH8601 QSPI display
  `init_sequence` still needs porting from `components/bsp/display.c`.
- `elecrow.yaml` — **second board target**: the ELECROW CrowPanel 2.1" ESP32
  Rotary Display (480×480). Compile- + `esphome config`-validated, **not yet
  hardware-validated**. See "Two boards, one component" below.

The **Google Cast control** (this project's reason for existing) has no ESPHome
equivalent and is Phase 3: a custom `external_components` C++ port of
`components/cast/`. The ESP-IDF firmware on `main` stays the working reference.

```bash
just esphome-config                # validate (fast)
just esphome-build                 # compile (uvx esphome; first run downloads the toolchain)
just esphome-run                   # build + flash + log (needs the board)
```

> After changing a shared component's IDF dependencies, run
> `uvx esphome clean esphome/knob.yaml` once — an incremental reconfigure can hit a
> spurious "Multiple ways to build … esp_efuse_fields.c.o"; a clean build fixes it.

## Progress (Phase 3)

`cast_controller` runs discovery **and Cast sessions** on a background FreeRTOS task
(so it never blocks ESPHome's main loop), reusing the whole shared stack
(`cast_discovery` + `cast_connection` + `cast_session` + `cast_status`, mDNS + TLS) —
all compile-verified to link. It exposes **Home Assistant entities**:

```yaml
cast_controller:
  devices_found: { name: "Cast devices found" }   # sensor
  now_playing:   { name: "Now playing" }           # text_sensor
  volume:        { name: "Cast volume" }            # sensor (%)
```

It also exposes HA **controls** — a volume `number` and play/pause/next/prev/mute
`button`s — routed to the session via a command queue. Runtime behaviour (discovery
→ session → entity values, and control round-trips) needs the board to validate; the
linkage and HA-entity/control codegen are proven. **Next:** device selection + a warm
session, then the LVGL UI (Phase 2) — the SH8601 display init still needs porting +
on-device validation.

## Two boards, one component

`cast_controller` now drives **two boards** from the one shared `components/cast/`
stack. The board-specific peripherals are **compile-optional** — Python emits a
`-DCAST_HAVE_*` flag only for what a YAML wires up, and the C++ `#ifdef`s match:

| | Waveshare (`knob.yaml`) | Elecrow 2.1 (`elecrow.yaml`) |
|---|---|---|
| Display | SH8601 **QSPI** 360×360 (custom `sh8601`) | ST7701S **RGB** 480×480 (built-in `st7701s`) |
| Touch | CST816, I2C 11/12, real INT/RST | CST816, I2C 38/39, INT/RST on **PCF8574** |
| Encoder | PCNT special-case (decoded *in* the component) | standard quadrature → stock `rotary_encoder` → `on_encoder_delta()` |
| Push button | none (swipe to pick speaker) | **yes** (PCF8574 P5 → open/select speaker) |
| Haptics | DRV2605 | none |
| `encoder:` key | `pcnt` (default) | `external` |

A detent does the same thing on both boards via the shared
`CastController::on_encoder_delta()`; only the *source* of the detent differs.

```bash
just esphome-elecrow-config    # validate (fast)
just esphome-elecrow-build     # compile
just esphome-elecrow-run       # build + flash + log (needs the board)
```

## SH8601 display component

ESPHome's built-in `qspi_dbi` mis-frames SH8601 QSPI commands, so this repo
ships a custom `display: platform: sh8601` (`esphome/components/sh8601/`) that
wraps the `esp_lcd_sh8601` IDF driver — the same one the vanilla firmware uses —
and shares the init table (`components/bsp/include/sh8601_init_cmds.h`). No
public ESPHome SH8601 component existed (esphome discussion #3229).
