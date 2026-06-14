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
- [ ] Supported-commands gating in the UI (dim next/prev when unsupported)

## M5 — Multi-device UX
- [x] Switch active device via **swipe left/right** (LVGL gesture → `ui_take_swipe`;
      `run_session` reopens the new device without rescanning) — built
- [x] Sticky selection: keep the chosen device active across rescans (match by id)
- [x] Device-list overlay (tap center): modal list of all speakers with cached
      per-device state (`g_cache`); knob/tap to pick, bg-tap/12s to dismiss
      (`device_list_overlay` + `ui_devlist_*`) — built
- [ ] Warm-session LRU for instant switching
- [x] Cast **group** handling — group volume on the knob (receiver), plus a
      members overlay (multizone: `MULTIZONE_STATUS`/`DEVICE_UPDATED`,
      `SET_DEVICE_VOLUME`) where the knob adjusts each member; `ca` bit 0x20
      group detection — built, **needs a real Cast group to validate**
- [ ] Album artwork: fetch `media.metadata.images[]`, decode JPEG → `lv_image` (stretch)

## Testing infra (see [07-testing.md](07-testing.md))
- [x] Design doc: trace capture + native unit tests
- [x] Extract pure `cast_status.{c,h}` parser (rewired `cast_session`) — build green
- [ ] `native` env + Unity; `cast_proto` round-trip tests; `just test`
- [ ] `cast trace` (`-DCAST_TRACE`) + `just trace`; capture fixtures on hardware
- [ ] `cast_status` fixture tests; wire into CI

## Speaker selection (UX) — done
- [x] Knob **press → device list** (opens instantly), **dial navigates**, press/tap
      **selects**. Dial on now-playing = volume; long-press = mute; play/pause on
      the on-screen transport button. (`run_session` → `device_list_overlay`)

## M6 — Polish & robustness
- [ ] Haptics (DRV2605) on detents/press
- [ ] Error/edge handling: device disappears, app stops, Wi-Fi drop
- [ ] Boot time + memory budget pass
- [ ] OTA updates (dual-app partitions already provisioned)
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
