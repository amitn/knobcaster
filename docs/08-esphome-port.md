# 08 — ESPHome port (feasibility + plan)

> Status: **exploratory** (branch `esphome-port`). Phase-1 hardware scaffold builds;
> the **shared-component reuse is proven** — `components/cast/` compiles into *both*
> the vanilla IDF firmware and the ESPHome firmware (one source of truth).

## Two targets, one shared core (working)

Both firmwares build the **same** `components/cast/` ESP-IDF component:

- **Vanilla** (`main`, PlatformIO + ESP-IDF) — uses it directly via `components/`.
- **ESPHome** (`esphome/knob.yaml`) — the `cast_controller` external component
  (`esphome/components/cast_controller/`) calls `add_idf_component(name="cast",
  path=…/components/cast)` so ESPHome's IDF build compiles it verbatim, then a thin
  C++ wrapper calls into it. `components/cast/idf_component.yml` declares its own
  cJSON dep, so it's self-contained and both targets resolve it identically.

Verified: `just esphome-build` links `cast_parse_type` from the shared `cast_status.c`
into the ESPHome firmware, and `just build` (vanilla) still compiles. No code is
duplicated — the Cast stack has a single home.

## Can we port to ESPHome? Yes — but with one big caveat

ESPHome runs on **ESP-IDF**, has an **`lvgl`** component, a **`cst816`** touchscreen,
a PCNT-based **`rotary_encoder`**, **`psram`**, **`wifi` + `captive_portal` + improv**
provisioning, and built-in **`ota`**. So the *scaffolding* (display, touch, encoder,
button, backlight, Wi-Fi, OTA, UI widgets) maps to existing ESPHome features.

**The catch:** the core of this project — **Google Cast control** (mDNS browse of
`_googlecast._tcp`, CASTV2 TLS sessions, the protobuf framing, the JSON status
parsers) — has **no ESPHome equivalent**. It must be ported as a **custom external
component** (C++). The good news: our `components/cast/` code is mostly portable C
that already uses `esp-tls`, the IDF `mdns` component, and cJSON — all available
under ESPHome's ESP-IDF — so it's a *wrap*, not a rewrite. But it's the bulk of the
effort, and it's where the device's value lives.

## Component mapping

| Current (ESP-IDF, this repo) | ESPHome equivalent |
|------------------------------|--------------------|
| SH8601 QSPI display (`bsp/display.c`, `esp_lcd_sh8601`) | `display: platform: qspi_dbi` + custom `init_sequence` (port the ~190 cmds) — **needs on-device validation** |
| CST816 touch (`bsp/touch.c`) | `touchscreen: platform: cst816` ✅ built-in |
| Encoder via PCNT (`bsp/knob.c`) | `sensor: platform: rotary_encoder` ✅ (PCNT). Note: our anti-glitch "departure-from-0" logic may still be needed — see [02](02-architecture.md) |
| Button GPIO0 | `binary_sensor: platform: gpio` ✅ |
| Backlight GPIO47 + sleep | `output: platform: ledc` / `light` + a timeout automation ✅ |
| Haptics DRV2605 (`bsp/haptics.c`) | **custom external component** (no built-in DRV2605) — small |
| Wi-Fi + provisioning (`wifi/`, `provisioning/`) | `wifi:` (AP fallback) + `captive_portal:` + `improv_serial:`/`esp32_improv:` ✅ |
| OTA (`components/ota`) | built-in `ota:` + ESPHome/HA dashboard (different mechanism, simpler) ✅ |
| Album art (`albumart/`, HTTPS + JPEG decode) | `online_image:` fetches + decodes JPEG/PNG over HTTP ✅ (close fit) |
| Fonts: Hebrew/Latin TTF subset | `font:` with a TTF + glyph ranges ✅ |
| **Cast: discovery/connection/proto/session/status (`cast/`)** | **custom external component** — the big lift; port the C++ |
| UI screens + orchestration (`ui/`, `app_main.c`) | `lvgl:` widgets (YAML) + lambdas/automations, or a custom component |

## What you gain vs. what it costs

**Gain:** Home Assistant integration (expose the knob's state/controls as HA
entities), ESPHome-dashboard OTA, declarative YAML config, improv (BLE/serial)
provisioning, and config-as-data maintenance.

**Cost:** Re-wrap the Cast stack as an external component; re-express the bespoke UI
(now-playing, album-art background, device list, volume arc) in `lvgl` YAML +
lambdas, which is fiddlier than our hand-written `ui.c`; and accept ESPHome's
abstractions/cadence. The pure-logic parsers (`cast_status`) and their host tests
carry over unchanged.

**Verdict:** Feasible and arguably worth it for HA users. It is **not** a one-shot —
realistically a multi-phase effort, with Phase 3 (the Cast external component) the
dominant chunk.

## Phased plan

1. **Hardware bring-up** (`esphome/knob.yaml`) — esp-idf + psram, SH8601 QSPI display,
   CST816 touch, rotary encoder, button, backlight, wifi, ota. Goal: the board boots
   under ESPHome and shows a test UI. *Riskiest single item: the SH8601 QSPI config.*
2. **LVGL UI** — now-playing screen (device name, title/artist, volume arc, transport
   buttons, progress bar) as `lvgl` widgets bound to `globals`.
3. **`cast_controller` external component** — port `cast/` (discovery, connection,
   proto, session, status) into an ESPHome `external_components` C++ component that
   exposes state (now-playing, volume, device list) and actions (set volume, mute,
   transport, switch device). **The core work.**
4. **Wire UI ↔ cast** via lambdas/automations; album art via `online_image`; haptics
   as a small custom component.
5. **HA + polish** — expose entities to Home Assistant, improv provisioning, sleep.

## Recommendation

Keep the ESP-IDF firmware on `main` as the reference/working build. Pursue this port
on `esphome-port` incrementally, validating each phase on hardware (especially the
SH8601 display in Phase 1) before moving on. Phase 3 reuses the existing Cast C/C++
nearly verbatim — that's the leverage that makes this port sane rather than a rewrite.
