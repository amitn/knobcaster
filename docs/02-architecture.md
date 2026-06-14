# 02 — Firmware Architecture

Framework: **ESP-IDF** (built via PlatformIO, `framework = espidf`). This mirrors
the reference project [**ESPCaster**](https://github.com/amitn/ESPCaster) — which
already implements Cast discovery + control on ESP-IDF — and the two board BSPs
([EmbeddedWizard](https://github.com/EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN),
[BlueKnob](https://github.com/joshuacant/BlueKnob)) that target this exact board.
We use those as **design references** and write our own Cast layer (clean rewrite
for the knob), built on `esp-tls`, the IDF `mdns` component, `cJSON`, and
`esp_lvgl_port`.

### Prior art / what we borrow
| From | Borrow (as reference, not vendored) |
|------|--------------------------------------|
| ESPCaster | Cast discovery + controller design, CASTV2 framing, wifi_manager pattern, LVGL GUI structure |
| EmbeddedWizard BSP | SH8601 QSPI init, CST816 touch, encoder (PCNT) decode, pin config |
| BlueKnob | Knob UX patterns, power/sleep handling, multi-screen nav |

## Layered view

```
            ┌──────────────────────────────────────────────┐
            │                   UI layer                    │
            │  LVGL screens · device list · now-playing     │
            │  volume ring · transport buttons              │
            └───────────────┬───────────────▲──────────────┘
       input events         │               │  state snapshots
   (rotate/press/touch)     ▼               │
            ┌──────────────────────────────────────────────┐
            │                App / controller               │
            │  AppState (active device, device list, focus) │
            │  maps intents → Cast commands; debounces vol  │
            └───────┬───────────────────────────▲──────────┘
        commands    │                            │  status events
                    ▼                            │
            ┌──────────────────────────────────────────────┐
            │                 Cast layer                    │
            │  CastDiscovery (mDNS)                         │
            │  CastConnection (TLS + CASTV2 framing)        │
            │  CastSession   (per-device app/media state)   │
            └───────┬───────────────────────────▲──────────┘
                    ▼                            │
            ┌──────────────────────────────────────────────┐
            │   Platform: esp_wifi · mdns · esp-tls/mbedTLS │
            │            · PSRAM · NVS (creds)              │
            └──────────────────────────────────────────────┘
```

## Modules

| Module | Responsibility | Key APIs |
|--------|----------------|----------|
| `wifi` | STA connect/reconnect, SoftAP, scan, NVS creds | `wifi_init/connect_to/start_ap/creds_*` (`esp_wifi`+`esp_netif`) |
| `provisioning` | SoftAP web form to capture home Wi-Fi creds | `prov_start/stop/take_creds` (`esp_http_server`) |
| `cast/discovery` | mDNS browse `_googlecast._tcp`, maintain device table | `discovery_poll()` → `CastDevice[]` (IDF `mdns`) |
| `cast/connection` | One TLS socket per device; frame/deframe CASTV2; PING/PONG | `connect()`, `send(ns, dest, json)`, `poll()` (`esp-tls`) |
| `cast/session` | Per-device receiver+media status; request IDs; transport cmds | `getStatus()`, `setVolume()`, `play()`, `pause()`, `stop()`, `next()`, `prev()` |
| `cast/protobuf` | Minimal CASTV2 `CastMessage` encode/decode | hand-rolled (~60 lines) or nanopb |
| `app/state` | Single source of truth: device list, active device, derived UI model | `AppState` struct + reducers |
| `ui/*` | LVGL screens & widgets; emits intents, renders `AppState` | `ui_init()`, `ui_render(state)` |
| `hal/knob` | Encoder decode (PCNT) + button debounce → intents | `espressif/knob`+`button` or `driver/pulse_cnt.h` |
| `hal/display` | SH8601 QSPI panel + LVGL flush + CST816 touch | `esp_lcd_sh8601` + `esp_lcd_touch_cst816s` + `esp_lvgl_port` |
| `hal/haptics` *(stretch)* | DRV2605 effects on detents/press (I2C 0x5A) | `haptic_pulse()` |

## Concurrency model

Two FreeRTOS tasks plus LVGL's tick, all pinned deliberately:

- **Core 1 — `ui_task`**: LVGL handler + input polling. Must stay responsive
  (target 30 fps). Never blocks on network.
- **Core 0 — `net_task`**: Wi-Fi, mDNS, all Cast TLS sockets, parsing. TLS reads
  and JSON parsing happen here, off the render path.

They communicate through:
- **Commands queue** (`ui_task` → `net_task`): `CastCommand{ deviceId, verb, arg }`.
- **State mailbox** (`net_task` → `ui_task`): latest immutable `AppState` snapshot
  behind a mutex (or a FreeRTOS queue of diffs). UI only ever *reads* a snapshot.

This keeps LVGL single-threaded (its requirement) and prevents a slow/blocking
TLS handshake from freezing the knob.

```
knob/touch ──intent──▶ ui_task ──CastCommand──▶ [queue] ──▶ net_task
                          ▲                                     │
                          └────────AppState snapshot────────────┘
```

## Wi-Fi provisioning (SoftAP + QR + web form)

No credentials are compiled in by default. The boot sequence picks creds in this
order, and falls back to on-device provisioning:

```
boot ─▶ wifi_init (STA)
     ─▶ creds? NVS  → else compiled secrets.h → else none
     ─▶ wifi_connect_to(ssid,pass), wait ≤15s
        └─ connected ──▶ run app
        └─ failed/none ─▶ run_provisioning():
              wifi_start_ap("CastKnob-XXXX")     # open SoftAP @ 192.168.4.1, APSTA
              prov_start()                        # esp_http_server
              ui_prov_show(QR, ap)                # LCD shows a QR code
              ┌── phone scans QR ──▶ joins "CastKnob-XXXX"
              │   opens 192.168.4.1 (wildcard handler serves the form on any URL)
              │   submits home SSID + password  (POST /save, url-decoded)
              └── prov_take_creds() ─▶ stop AP+server ─▶ wifi_connect_to()
                     connected ─▶ wifi_creds_save() to NVS ─▶ run app
                     failed    ─▶ reopen the portal
```

- **QR** encodes a `WIFI:S:CastKnob-XXXX;T:nopass;;` join string (LVGL `lv_qrcode`,
  `CONFIG_LV_USE_QRCODE`). Scanning it joins the setup AP; the captive form then
  loads. A wildcard `GET /*` handler serves the form for captive-portal probes.
- **Persistence:** working creds are stored in NVS (namespace `wifi`) and reused
  on the next boot; the compiled `secrets.h` is only a fallback for dev.
- **Status:** a Wi-Fi icon on the now-playing screen is green when connected, red
  when not (`ui_set_wifi`). A persistent drop at runtime re-opens the portal.
- *Not a DNS captive portal yet* — auto-popup relies on the phone probing `/*`;
  worst case the user opens `192.168.4.1` manually. A 53/udp DNS hijack is a TODO.

## Connection strategy

Opening a TLS session to **every** discovered device at once is expensive (RAM +
sockets). Strategy:

- **Discovery** is always-on and cheap (mDNS), so the *list* of devices is always
  fresh without any TLS.
- **Status fan-out (lazy):** maintain a TLS connection to the **active** device
  for live updates. Optionally keep a small LRU pool (e.g. 2–3) of recently
  viewed devices warm so switching feels instant.
- To populate "what's playing" for the *list* without N sockets, poll devices
  round-robin: connect → `GET_STATUS` → read → disconnect, throttled. Cache the
  result with a TTL. (MVP can skip list-previews and only show now-playing for the
  active device.)

## State model (sketch)

```cpp
struct CastDevice {
  String   id;            // mDNS instance / friendly id
  String   friendlyName;  // "Kitchen speaker"
  IPAddress ip;
  uint16_t port;          // 8009
  String   model;         // from mDNS TXT "md="
  bool     isGroup;       // Cast group vs single device
};

struct MediaStatus {
  enum State { IDLE, BUFFERING, PLAYING, PAUSED } state = IDLE;
  String   title, subtitle, appName;   // "Bad Guy", "Billie Eilish", "Spotify"
  bool     supportsPause, supportsNext, supportsPrev;
  int      mediaSessionId;
};

struct VolumeStatus { float level = 0; bool muted = false; };

struct AppState {
  std::vector<CastDevice> devices;
  int          activeIndex = -1;
  MediaStatus  media;     // of active device
  VolumeStatus volume;    // of active device
  enum Conn { DISCONNECTED, CONNECTING, READY, ERROR } conn = DISCONNECTED;
  String       statusLine;  // transient UI hint
};
```

## Volume handling (the latency-sensitive path)

Knob rotation generates many events fast. Don't send a Cast `SET_VOLUME` per
detent:

1. Apply rotation to a **local optimistic** volume immediately (UI ring moves at
   once — no perceived lag).
2. **Debounce/rate-limit** outgoing `SET_VOLUME` to ~10–20 Hz (e.g. send the
   latest target every 50–80 ms while turning, plus a final send on stop).
3. **Reconcile** when the device's `RECEIVER_STATUS` echoes back; if it diverges
   (someone else changed it), snap the UI to the authoritative value when the
   knob is idle.

## Error handling & resilience

- Wi-Fi drop → `net_task` reconnects with backoff; UI shows a banner; commands
  queue is drained/rejected meanwhile.
- TLS/socket error to a device → mark device `ERROR`, retry with backoff, keep
  discovery list intact.
- Heartbeat: send `PING` on the `tp.heartbeat` namespace; if no `PONG` within
  ~10 s, consider the connection dead and reconnect.
- Watchdog: long blocking calls live only in `net_task`; `ui_task` feeds the WDT.

## Directory layout (planned)

ESP-IDF / PlatformIO structure. `src/` is the IDF "main" component (its
`idf_component.yml` pulls managed deps); reusable layers can graduate into
`components/`.

```
src/                    # IDF main component
  app_main.c            # app_main(): init HAL, wifi, tasks
  idf_component.yml     # managed deps (lvgl, mdns, esp_lcd_sh8601, ...)
  CMakeLists.txt
components/
  cast/                 # our Cast layer (clean rewrite; ESPCaster as reference)
    discovery.*         # mDNS browse
    connection.*        # CASTV2 framing + esp-tls
    session.*           # receiver/media status + transport cmds
    protobuf.*          # minimal CastMessage encode/decode
    cast_message.proto  # reference schema
  app/
    state.*             # AppState + reducers
    controller.*        # intent → command mapping, volume debounce
  hal/
    display.*           # SH8601 (esp_lcd) + esp_lvgl_port flush
    touch.*             # CST816
    knob.*              # encoder (PCNT) + button
  net/
    wifi.*
  ui/                   # LVGL screens & widgets
include/
  secrets.h             # (gitignored) WIFI_SSID / WIFI_PASS
  lv_conf.h             # LVGL config
sdkconfig.defaults      # PSRAM octal, 16MB flash, TLS, console
partitions.csv
```

> Code sketches in these docs are illustrative C++-ish pseudocode. In ESP-IDF
> the real types are C: fixed `char[]` / `cJSON` instead of `String`/ArduinoJson,
> arrays instead of `std::vector`, `esp_ip4_addr_t` instead of `IPAddress`.
