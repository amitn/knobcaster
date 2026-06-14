# 03 — Google Cast (CASTV2) Protocol

This is the reverse-engineered, LAN-local protocol that the Chromecast / Google
Cast apps use. It is **undocumented by Google** and may change; treat it as
best-effort. It needs **no** Google account — everything is on the local network.

Implementation here is **ESP-IDF** (`esp-tls`, IDF `mdns`, `cJSON`). Our own
[ESPCaster](https://github.com/amitn/ESPCaster) (`components/chromecast_controller`,
`components/chromecast_discovery`) already does this on ESP-IDF and is the primary
reference — we rewrite for the knob rather than vendoring it.

> Cross-check behavior against:
> [ESPCaster](https://github.com/amitn/ESPCaster) (ESP-IDF, same author),
> [pychromecast](https://github.com/home-assistant-libs/pychromecast),
> [node-castv2](https://github.com/thibauts/node-castv2),
> [CastChannel proto](https://github.com/thibauts/node-castv2/blob/master/lib/cast_channel.proto).

## 1. Discovery — mDNS

Browse for the service type:

```
_googlecast._tcp.local
```

Each responder yields:
- **A record** → device IP.
- **SRV record** → port (almost always **8009**).
- **TXT record** → key/value metadata. Useful keys:

| TXT key | Meaning |
|---------|---------|
| `fn` | friendly name ("Kitchen speaker") |
| `md` | model ("Google Nest Mini") |
| `id` | device UUID |
| `ca` | capabilities bitmask (bit indicates audio/video/group…) |
| `rs` | current status text (e.g. app/"now playing" hint) |
| `cd` | (groups) |

ESP-IDF: `mdns_query_ptr("_googlecast", "_tcp", timeout, max, &results)`, then walk
`mdns_result_t` for IP, port, and `txt`/`txt_value` arrays. Re-query periodically
(e.g. every 10–30 s) and age out devices that stop responding.

> Cast **groups** also advertise here; distinguish via the `ca`/`md` TXT and
> handle group volume semantics (group volume is relative to member volumes).

## 2. Transport — TLS to port 8009 {#tls}

- Open a **TLS** socket to `deviceIp:8009`.
- Devices present **self-signed certificates** → connect *without* CA
  verification. ESP-IDF `esp-tls`:

```c
esp_tls_cfg_t cfg = {
    .skip_common_name = true,        // self-signed; don't validate CN
    .crt_bundle_attach = NULL,       // no CA bundle = no chain verification
    .timeout_ms = 10000,
};
esp_tls_t *tls = esp_tls_init();
if (esp_tls_conn_new_sync(host_ip_str, strlen(host_ip_str), 8009, &cfg, tls) != 1) {
    /* retry with backoff */
}
// then esp_tls_conn_read() / esp_tls_conn_write()
```

- Budget RAM: each TLS session needs sizeable buffers. Prefer one active session;
  with `CONFIG_MBEDTLS_DYNAMIC_BUFFER` + PSRAM, mbedTLS buffers can grow off the
  internal heap.

## 3. Framing — length-prefixed protobuf

Every message on the wire is:

```
┌────────────────────┬───────────────────────────────┐
│ uint32 big-endian  │  serialized CastMessage proto  │
│ length = N         │  (N bytes)                     │
└────────────────────┴───────────────────────────────┘
```

Read 4 bytes → `N` → read exactly `N` bytes → decode `CastMessage`. Writing is
the reverse. Watch for partial reads on the non-blocking socket.

### CastMessage definition

```proto
message CastMessage {
  enum ProtocolVersion { CASTV2_1_0 = 0; }
  required ProtocolVersion protocol_version = 1;   // 0
  required string source_id      = 2;              // e.g. "sender-0"
  required string destination_id = 3;              // "receiver-0" or transport id
  required string namespace      = 4;              // "urn:x-cast:com.google.cast.*"
  enum PayloadType { STRING = 0; BINARY = 1; }
  required PayloadType payload_type = 5;           // 0 (STRING)
  optional string payload_utf8   = 6;              // JSON string lives here
  optional bytes  payload_binary = 7;
}
```

For our needs `payload_type` is always `STRING` and the payload is a **JSON**
string in `payload_utf8`. We never use `payload_binary`.

### Minimal hand-rolled protobuf

We don't need nanopb — `CastMessage` is tiny and fixed. Encode the 6 fields with
protobuf wire format directly:

- Fields 2,3,4,6 are **length-delimited** strings → tag `(field<<3)|2`, then a
  varint length, then bytes.
- Fields 1,5 are **varint** enums → tag `(field<<3)|0`, then a varint (`0`).

Decoding: loop reading `tag = varint`; `field = tag>>3`, `wire = tag&7`; for
strings read varint length + bytes; for varints read a varint; dispatch by field
number. A ~60-line `cast/protobuf.*` covers encode+decode. (You can `0`-default
the required varints to keep the encoder trivial.)

## 4. Virtual channels & namespaces

Multiple logical channels are multiplexed over the one TLS socket, addressed by
`(source_id, destination_id, namespace)`:

| Namespace | Purpose | Key messages |
|-----------|---------|--------------|
| `…tp.connection` | open/close a virtual connection | `CONNECT`, `CLOSE` |
| `…tp.heartbeat` | keepalive | `PING`, `PONG` |
| `…receiver` | device-level: apps + volume | `GET_STATUS`, `SET_VOLUME`, `LAUNCH`, `STOP` |
| `…media` | media session transport | `GET_STATUS`, `PLAY`, `PAUSE`, `STOP`, `SEEK`, `QUEUE_UPDATE` |

(`…` = `urn:x-cast:com.google.cast`.)

`source_id` is ours (use `sender-0`). `destination_id` starts as `receiver-0` for
device-level traffic; for **media** you must first learn the running app's
`transportId` from the receiver status and use *that* as the destination.

## 5. Message flows

### 5.1 Handshake (once per connection)

```
→ connection  CONNECT   {"type":"CONNECT"}                       dest=receiver-0
→ heartbeat   PING      {"type":"PING"}            (repeat ~5s)  dest=receiver-0
→ receiver    GET_STATUS {"type":"GET_STATUS","requestId":1}     dest=receiver-0
← receiver    RECEIVER_STATUS { volume, applications:[…] }
```

From `RECEIVER_STATUS`:
- `status.volume.level` (0.0–1.0) and `status.volume.muted` → **VolumeStatus**.
- `status.applications[0]` → `{ appId, displayName ("Spotify"), transportId,
  sessionId, namespaces[] }`. If `applications` is empty → device is **idle**
  (nothing playing).

### 5.2 Subscribe to the media session

Open a **second** virtual connection addressed to the app's `transportId`, then
query media status:

```
→ connection CONNECT    {"type":"CONNECT"}                 dest=<transportId>
→ media      GET_STATUS {"type":"GET_STATUS","requestId":2} dest=<transportId>
← media      MEDIA_STATUS { status:[ { mediaSessionId, playerState,
                                       media:{ metadata:{title, artist,…} },
                                       supportedMediaCommands } ] }
```

Map it:
- `playerState` ∈ {`IDLE`,`BUFFERING`,`PLAYING`,`PAUSED`} → `MediaStatus.state`.
- `media.metadata.title` / `artist` / `subtitle` / `images[]` → now-playing.
- `supportedMediaCommands` bitmask → enable/disable Next/Prev/Pause in the UI.
- Remember `mediaSessionId` — required for every transport command.

### 5.3 Live updates (no polling needed for the active device)

After the connection + GET_STATUS, the device **pushes** `RECEIVER_STATUS` and
`MEDIA_STATUS` whenever things change (track change, external volume change, app
launch/stop). Just keep reading the socket and reduce updates into `AppState`.
Keep sending `PING` so the device keeps the channel alive.

## 6. Commands we send

All transport commands go to `…media`, `dest=<transportId>`, and **must** carry
the current `mediaSessionId` and a fresh `requestId`.

| Intent | Namespace | JSON payload |
|--------|-----------|--------------|
| Play | media | `{"type":"PLAY","mediaSessionId":S,"requestId":R}` |
| Pause | media | `{"type":"PAUSE","mediaSessionId":S,"requestId":R}` |
| Stop media | media | `{"type":"STOP","mediaSessionId":S,"requestId":R}` |
| Next | media | `{"type":"QUEUE_UPDATE","mediaSessionId":S,"jump":1,"requestId":R}` |
| Previous | media | `{"type":"QUEUE_UPDATE","mediaSessionId":S,"jump":-1,"requestId":R}` |
| Seek | media | `{"type":"SEEK","mediaSessionId":S,"currentTime":T,"requestId":R}` |
| Set volume | receiver | `{"type":"SET_VOLUME","volume":{"level":0.42},"requestId":R}` |
| Mute | receiver | `{"type":"SET_VOLUME","volume":{"muted":true},"requestId":R}` |
| Stop app | receiver | `{"type":"STOP","sessionId":"…","requestId":R}` |

Notes:
- **Next/Prev** is `QUEUE_UPDATE` with `jump: ±1` (older receivers also accept
  `QUEUE_NEXT`/`QUEUE_PREV` types — `QUEUE_UPDATE` is the portable choice).
- Volume `level` is a float 0.0–1.0. Some devices quantize to fixed steps; rely on
  the echoed `RECEIVER_STATUS` for the true value.
- Every command should expect either a typed reply or an `INVALID_REQUEST` /
  `LOAD_FAILED` error keyed by `requestId`.

## 7. Request/response correlation

- Maintain a monotonically increasing `requestId`.
- Responses echo the `requestId` → match outstanding requests, time them out.
- Some pushed messages have `requestId: 0` (unsolicited) — treat as broadcasts.

## 8. Heartbeat & timeouts

- Send `PING` (heartbeat ns, dest `receiver-0`) every ~5 s.
- If no `PONG`/traffic for ~10 s → assume dead, close, reconnect with backoff.
- Reconnect must redo the full handshake (5.1 → 5.2).

## 9. Reference: namespace constants

```cpp
constexpr auto NS_CONNECTION = "urn:x-cast:com.google.cast.tp.connection";
constexpr auto NS_HEARTBEAT  = "urn:x-cast:com.google.cast.tp.heartbeat";
constexpr auto NS_RECEIVER   = "urn:x-cast:com.google.cast.receiver";
constexpr auto NS_MEDIA      = "urn:x-cast:com.google.cast.media";
constexpr auto SRC_DEFAULT   = "sender-0";
constexpr auto DST_RECEIVER  = "receiver-0";
constexpr uint16_t CAST_PORT = 8009;
```

## 10. Implementation checklist

- [ ] mDNS browse + TXT parse → device table
- [ ] TLS connect via `esp-tls` (`skip_common_name`) to 8009
- [ ] 4-byte length framing read/write (handle partial reads)
- [ ] `CastMessage` encode/decode (minimal protobuf)
- [ ] JSON build/parse (cJSON) for payloads
- [ ] Handshake: CONNECT + PING loop + GET_STATUS
- [ ] Parse RECEIVER_STATUS → volume + app transportId
- [ ] Second CONNECT to transportId + media GET_STATUS
- [ ] Parse MEDIA_STATUS → now-playing + mediaSessionId + supported commands
- [ ] Send transport commands (PLAY/PAUSE/STOP/QUEUE_UPDATE)
- [ ] SET_VOLUME with debounce + reconcile
- [ ] Heartbeat timeout → reconnect with backoff
