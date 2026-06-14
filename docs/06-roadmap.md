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
- [x] `include/secrets.h.example` (lv_conf.h deferred to M1 display)

## M1 — Hardware bring-up
- [ ] Confirm the **push-button GPIO** on hardware (only remaining pin unknown)
- [ ] ST77916 QSPI panel up; solid-color + test pattern at 360×360
      (reference EmbeddedWizard `ew_bsp_display.c`)
- [ ] LVGL 9 via `esp_lvgl_port` with PSRAM draw buffer; "hello" screen
- [ ] CST816 touch (`esp_lcd_touch_cst816s`) → LVGL pointer indev
- [ ] Encoder via PCNT + button → events

## M2 — Network & discovery
- [x] Wi-Fi connect from `secrets.h`, reconnect/backoff (`components/wifi`)
- [x] mDNS browse `_googlecast._tcp`; parse TXT (`fn`,`md`,`id`) → `cast_device_t[]`
      (`components/cast/cast_discovery.c`); logs device list each scan
- [ ] Render discovered device list on screen (needs M1 display)

## M3 — Cast read path  (reference: ESPCaster `chromecast_controller`)
- [x] TLS connect to `:8009` via `esp-tls` (`skip_common_name`) — `cast_connection.c`
- [x] CASTV2 framing + minimal `CastMessage` protobuf encode/decode — `cast_proto.c`
- [x] Handshake probe: CONNECT + GET_STATUS, log raw replies (`probe_device`)
- [ ] PING/PONG keepalive
- [ ] Parse RECEIVER_STATUS → volume + app transportId (cJSON)
- [ ] Media CONNECT + GET_STATUS → now-playing + mediaSessionId
- [ ] Live status updates reduce into `AppState`; show now-playing on screen

> Builds clean; **untested on hardware/real Cast device** — the probe path needs
> on-device validation (TLS handshake, framing, actual payloads).

## M4 — Cast control path
- [ ] SET_VOLUME from knob with debounce + reconcile
- [ ] Play / Pause via knob press
- [ ] Stop / Next / Prev via touch
- [ ] Mute (long-press) and supported-commands gating

## M5 — Multi-device UX
- [ ] Switch active device (swipe / list)
- [ ] Device-list overlay with cached per-device state
- [ ] Warm-session LRU for instant switching
- [ ] Cast **group** handling (group vs member volume)

## M6 — Polish & robustness
- [ ] Haptics (DRV2605) on detents/press
- [ ] Error/edge handling: device disappears, app stops, Wi-Fi drop
- [ ] Boot time + memory budget pass
- [ ] OTA updates (dual-app partitions already provisioned)
- [ ] BLE/captive-portal Wi-Fi provisioning (replace compiled-in creds)

## Open questions

1. **Push-button GPIO** — the one pin not in the EmbeddedWizard BSP. Confirm on
   hardware (candidate GPIO0/BOOT). Encoder A/B (8/7) and quadrature are confirmed.
2. **TLS memory footprint** — how many concurrent Cast TLS sessions fit in RAM
   alongside LVGL? Drives the connection strategy (single vs warm-pool).
3. **List previews** — do we want now-playing for *every* device in the list
   (round-robin polling cost) or only the active one for MVP?
4. **Next/Prev portability** — `QUEUE_UPDATE {jump:±1}` vs `QUEUE_NEXT/PREV`
   across receiver apps (Spotify/YT Music/default). Needs device testing.
5. **Groups** — volume math for Cast groups (member vs group level) and whether
   transport even applies.
6. **Secondary ESP32** — confirmed unused? Any shared-bus contention (audio DAC
   signals reportedly reach both MCUs)?
7. **Provisioning** — acceptable to ship compiled-in creds for v1, or is
   BLE/portal provisioning required from the start?

## Risks

| Risk | Mitigation |
|------|------------|
| Cast protocol changes (undocumented) | Pin behavior to pychromecast/node-castv2; isolate in `cast/` |
| Wrong pinout bricks bring-up time | Verify schematic first; M1 gates everything |
| RAM exhaustion (TLS+LVGL+JSON) | PSRAM buffers; single warm session; stream-parse JSON |
| Round display edge clipping | Keep UI inside safe inner circle |
