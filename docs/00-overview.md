# 00 — Overview & Goals

## Elevator pitch

A desk knob that controls every Chromecast/Google Cast **audio** device on your
home network. No phone, no app — physical rotation for volume, a press for
play/pause, and a touch screen to hop between speakers and see what's playing.

## Prior art (we stand on these)

- [**ESPCaster**](https://github.com/amitn/ESPCaster) — same author's ESP-IDF
  project that already discovers and controls Chromecast devices with an LVGL UI
  (on the 1.46B board). Primary design reference for the Cast layer.
- [**EmbeddedWizard BSP**](https://github.com/EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN)
  — ESP-IDF BSP for *this exact knob board*; confirmed pin map + driver init.
- [**BlueKnob**](https://github.com/joshuacant/BlueKnob) — BLE media remote on
  this board; knob UX and power-management patterns.

We target **ESP-IDF** (built via PlatformIO) to reuse those designs, and write our
own Cast layer (clean rewrite for the knob). See
[02-architecture.md](02-architecture.md).

## Goals (MVP)

1. **Join Wi-Fi** using stored credentials (with a fallback provisioning path).
2. **Discover** all Google Cast devices on the LAN via mDNS (`_googlecast._tcp`).
3. **List & iterate** discovered devices on the display; select one as "active".
4. **Show now-playing** for the active device: title, artist/subtitle, play state,
   and current volume.
5. **Control** the active device:
   - Volume up/down (knob rotation) and mute.
   - Play / Pause (knob press).
   - Stop, Next, Previous (touch controls).
6. **Stay live**: reflect external changes (someone else changes volume / track)
   via Cast status updates.

## Stretch goals

- Group (multi-room) volume handling for Cast **groups**.
- Haptic feedback (DRV2605) on detents and button presses.
- Audio-reactive idle screen using the onboard mic / DAC visualization.
- Per-device "favorites" and quick re-select.
- OTA firmware updates (dual-app partition layout is already provisioned).

## Explicit non-goals (for MVP)

- **Casting media** *to* devices (we control existing sessions, we don't launch
  our own media receiver — though `LAUNCH`/`STOP` of the default media receiver
  is in scope for transport control).
- Video device UX beyond basic transport (focus is audio/speakers).
- Cloud / account integration. Everything is LAN-local and unauthenticated at the
  network layer, which is exactly how the Cast local protocol works.
- Using the **secondary ESP32** co-processor. All logic targets the ESP32-S3.

## Key constraints & realities

- **Local Cast protocol only.** We talk the on-device CASTV2 protocol over TLS on
  port 8009. This is unauthenticated on the LAN and needs no Google account, but
  it is undocumented/reverse-engineered and can change.
- **TLS with self-signed certs.** Cast devices present self-signed certificates;
  the client connects "insecure" (no CA verification). See
  [03-cast-protocol.md](03-cast-protocol.md#tls).
- **Memory.** TLS + JSON + LVGL + framebuffers is heavy. The board's **8 MB PSRAM**
  is essential; LVGL draw buffers and large allocations go to PSRAM.
- **Hardware caveats.** Pin assignments and the exact encoder behavior must be
  confirmed against the Waveshare schematic/demo — see
  [01-hardware.md](01-hardware.md#verify-before-you-trust).

## Success criteria for MVP

> Power on → within ~10 s the screen lists my speakers → I pick the kitchen
> speaker → I see the song that's playing → I turn the knob and the kitchen
> speaker's volume changes within ~200 ms → I press and it pauses.
