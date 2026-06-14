# 07 — Testing & Offline Debugging

The Cast layer talks to undocumented, hardware-only services, so most of it has
only ever been *build-verified*. This doc specifies how we make it testable
**without** hardware: capture real traffic once, then replay it in fast host-side
unit tests. (Status: **planned** — this is the spec; nothing here is built yet.)

## Goals

1. **Capture** real CASTV2 traffic (the JSON payloads + framed bytes) from a live
   session so we have ground-truth fixtures.
2. **Unit-test the pure logic** — protobuf framing and JSON→struct parsing —
   on the host (`pio test -e native`, Unity), fast and hardware-free.
3. **Regression-proof** the parsers against the captured fixtures (incl. groups).

Non-goal: end-to-end/integration testing of TLS/mDNS/LVGL — those stay manual on
hardware (see [`just shot`](05-build-and-tooling.md) for screen capture).

## What's pure (testable) vs not

| Code | Deps | Testable on host? |
|------|------|-------------------|
| `cast_proto.c` (CastMessage encode/decode, varint) | `string.h` only | ✅ directly |
| JSON status parsing (receiver/media/multizone) | cJSON only | ✅ after the refactor below |
| `cast_connection.c` (esp-tls, sockets) | ESP-IDF | ❌ hardware/integration |
| `cast_discovery.c` (mdns) | ESP-IDF | ❌ |
| `cast_session.c` orchestration (timers, send) | ESP-IDF | ❌ (but its parsing is extracted) |

### Refactor: extract a pure parser

Move the JSON parsing out of `cast_session.c` into a dependency-free
`components/cast/cast_status.c` (+ `.h`) that only needs cJSON:

```c
// cast_status.h — pure, no ESP deps; safe to compile on host.
bool cast_parse_receiver_status(const char *json, size_t len,
                                cast_volume_status_t *vol,
                                char *transport_id, size_t tid_cap,
                                char *app_name, size_t app_cap);
bool cast_parse_media_status (const char *json, size_t len, cast_media_status_t *out);
int  cast_parse_multizone    (const char *json, size_t len,
                              cast_member_t *out, int max);   // returns member count
```

`cast_session.c` then calls these; the native tests call the same functions on
fixture strings. cJSON for the native env is supplied via `lib_deps`
(`espressif/cjson` mirror / vendored `cJSON.c`).

## Trace capture (firmware)

Add an opt-in **cast trace** that logs every frame to the serial console in a
greppable, one-line-per-message format. It hooks `cast_conn_send` /
`cast_conn_recv`:

```
CAST RX <namespace> <payload-json>
CAST TX <namespace> <payload-json>
```

- Gated by a build flag (`-DCAST_TRACE=1`) or a runtime toggle, off by default.
- Payload is the raw JSON, single-lined; the framed protobuf bytes can optionally
  be emitted hex-encoded for `cast_proto` decode tests (`CAST RXB <hex>`).
- Capture with `just trace` (to be added): runs the serial monitor and tees to
  `test/fixtures/capture-<label>.log`. A small script slices that log into
  individual `*.json` / `*.bin` fixtures by namespace + type.

Exercise the device (play/pause, change volume, a group, multi-device) to cover
the message types we parse.

## Serial debug console (on-device input emulation)

The `fbdump` task doubles as a serial debug console over USB-Serial/JTAG — useful
for driving the UI without the physical knob/touch (and for scripted tests):

| Key | Action |
|-----|--------|
| `S` | screenshot (framebuffer → `just shot`) |
| `+` / `-` | knob rotate CW / CCW (one detent) — volume or speaker-pick per mode |
| `p` | knob short press (toggle dial mode) |
| `l` | knob long press (mute) |

Implemented by injecting at the knob source (`knob_inject_delta/press/...`) so all
downstream logic runs unchanged.

**TODO — extend the debug console:**
- [ ] Absolute volume set (e.g. `v50`), and mute toggle
- [ ] Emulate touch: swipe left/right (device switch), tap-center (list),
      transport buttons (prev/play/next), tap a list row
- [ ] A `just emit <keys>` recipe (send a command sequence over serial)
- [ ] Scripted UI smoke test: emit input + screenshot + assert pixels/log

## Native unit tests

PlatformIO `native` environment + Unity (`pio test -e native`):

```
test/
  fixtures/                 # captured ground-truth payloads
    receiver_status_spotify.json
    media_status_playing.json
    multizone_status_group.json
    castmessage_get_status.bin     # framed bytes for proto tests
  test_proto/               # cast_proto encode/decode + varint edges
    test_proto.c
  test_status/              # cast_status parsers vs fixtures
    test_status.c
```

`platformio.ini`:

```ini
[env:native]
platform = native
build_flags = -DUNIT_TEST
lib_deps = <cjson>          # or vendored cJSON.c
test_framework = unity
```

### Coverage targets

- **`cast_proto`**: encode→decode round-trips; decode of captured `*.bin`; varint
  multi-byte + truncation; oversized/short frames rejected.
- **`cast_status`**: each fixture parses to the expected struct — title/artist,
  `playerState`, `mediaSessionId`, `supportedMediaCommands` bits, volume level/
  muted, app `displayName`/`transportId`; idle (no `applications`); multizone
  members (ids, names, levels) and `DEVICE_UPDATED` merge.
- **Edge/malformed**: empty payload, missing keys, wrong types → no crash, sane
  defaults.

## Just recipes (to add)

| Recipe | Action |
|--------|--------|
| `just test` | `uv run pio test -e native` (host unit tests) |
| `just trace [label]` | capture serial trace → `test/fixtures/capture-<label>.log` |
| `just fixtures` | slice a capture log into per-message fixture files |

## CI (later)

`just test` runs on every PR (host-only, no hardware) alongside the existing
`just build` compile gate — see [05-build-and-tooling.md](05-build-and-tooling.md).

## Implementation order

1. Extract `cast_status.{c,h}`; rewire `cast_session.c` to use it (build stays green).
2. Add the `native` env + Unity + a first `cast_proto` round-trip test (synthetic).
3. Add `cast trace` + `just trace`; capture real fixtures on hardware.
4. Add `cast_status` fixture tests; wire `just test` into CI.
