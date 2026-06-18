# 04 — UI / UX

Surface: a **360 × 360 round** touch LCD, a **rotary encoder with push**, and
(stretch) **haptics**. Two primary gestures define the experience:

- **Dial (rotate)** → **volume** of the active device.
- **Swipe left / right** → **select device** (cycle through discovered speakers).

Everything else is secondary.

## Interaction model

| Gesture | Action |
|---------|--------|
| **Rotate dial** | Now-playing: **volume** · Device list open: **move the highlight** |
| **Press dial** | Open the **device list**; in the list, press again **selects** the highlighted speaker |
| **Long-press dial** | **Mute / unmute** the active device |
| **Swipe left / right** | Quick **select device** (prev/next discovered speaker) |
| **Tap play/pause button** | Play / Pause (on-screen transport) |
| **Tap ◀◀ / ▶▶ buttons** | Previous / Next track |
| **Tap center** | Also opens the device list (group → member volumes) |
| **Tap a device in list** | Make it active, return to now-playing |

The device list opens instantly (names + cached state where known) and is
navigated with the dial — the active device is marked `>` and highlighted.

The two primaries are deliberately split across the two input surfaces — the
**dial owns volume**, **swipe owns device selection** — so they never compete.
The device list (tap center) is just a faster path to the same selection that
swiping cycles through.

### Now-playing extras (implemented)

- **Album art** — the current track's cover loads asynchronously and shows as a
  dimmed full-screen background behind the text (`ui_set_art`, `components/albumart`).
- **Per-speaker volume color** — the volume ring's color is unique per speaker
  (hashed from its name), so you can tell at a glance which one you're on; the
  ring turns red while muted.
- **Any-script titles** — titles render via LVGL Tiny-TTF with an embedded
  DejaVu subset (Latin + Hebrew) and bidi, so RTL/non-Latin titles display
  correctly instead of tofu boxes.
- **Track-position bar** — a thin progress bar under the title reflects
  `currentTime`/`duration` from the media status. Cast only pushes `currentTime`
  occasionally, so the position is anchored to a local timestamp and interpolated
  while playing (`render_session`); hidden for live streams (no duration) and when
  idle. Parser (`cast_status`) is host-unit-tested.
- **Haptic detents** — a DRV2605 click fires on each detent (the encoder is
  mechanically smooth).
- **Screen sleep** — after 5 min idle the backlight + panel turn off; any input
  wakes it instantly (and that first input is swallowed). Cast stays connected,
  so the screen shows live state the moment it wakes.

> Single source of truth: the UI only renders an `AppState` snapshot and emits
> *intents*; it never talks to the network directly (see
> [02-architecture.md](02-architecture.md#concurrency-model)).

## Speaker selection: press → list (implemented)

On the now-playing screen the dial controls **volume**. **Press** opens the
**device list** overlay; the dial then **navigates** it and a second press (or a
tap) **selects** the highlighted speaker:

- **Press** (now-playing) → open the device list. It opens instantly: device
  names plus cached state where known; the active device is marked `>` and
  highlighted.
- **Rotate** (list open) → move the highlight. **Press again** (or tap a row) →
  select that speaker and open its session. Background tap / 12 s timeout cancels.
- **Play/pause** lives on the on-screen center transport button. **Long-press** =
  mute. Swipe still does a quick prev/next device switch.

Implemented in `run_session` (press → `SESSION_OPEN_LIST`) + `device_list_overlay`.

## Screens

### A. Now-Playing (home)

```
        ╭───────────────────────╮
        │        Kitchen        │   device friendly name (tap → list)
        │                       │
        │     ▶   Bad Guy       │   play icon + title
        │       Billie Eilish   │   artist / subtitle
        │                       │
        │   ◀◀    ❚❚    ▶▶       │   prev · play/pause · next (touch)
        │  ▰▰▰▰▰▰▰▱▱▱▱▱▱▱  62%  │   volume ring (follows dial)
        ╰───────────────────────╯
            ‹ swipe to change speaker ›   dial = volume
```

- The **volume ring** is an arc widget around the perimeter; the knob drives it.
- Play/pause icon reflects `MediaStatus.state`; Next/Prev dim when
  `supportedMediaCommands` says they're unavailable.
- Album art (from `media.images[]`) optional — fetch is extra RAM/network; MVP can
  show a colored placeholder.

### B. Device List (overlay)

```
        ╭───────────────────────╮
        │      Speakers (3)     │
        │  ● Kitchen     ▶ 62%  │   active (●), playing, volume
        │  ○ Office      ❚❚      │
        │  ○ Living room  idle  │
        ╰───────────────────────╯
```

- Knob rotation scrolls the list; press selects; tap selects.
- Shows per-device state if available (lazy/cached — see connection strategy).
- Cast **groups** badged distinctly.

### D. Wi-Fi setup (provisioning)

Shown on first boot or when Wi-Fi can't connect (see
[02-architecture.md](02-architecture.md#wi-fi-provisioning-softap--qr--web-form)):

```
        ╭───────────────────────╮
        │     Set up Wi-Fi      │
        │     ▛▀▀▜  ▛▀▜ ▛▜       │   QR (scan to join CastKnob-XXXX)
        │     ▙▄▄▟  ▙▄▟ ▙▟       │
        │  scan, or join        │
        │  CastKnob-A1B2         │
        │  then open 192.168.4.1 │
        ╰───────────────────────╯
```

A small **Wi-Fi icon** at the top of the now-playing screen shows connection
status (green = connected, red = not).

### C. Status / transient states

- **Booting / Wi-Fi**: spinner + "Connecting to <SSID>…".
- **Scanning**: "Looking for speakers…" until first device appears.
- **No devices**: hint to check that speakers are on the same network.
- **Error banner**: non-blocking toast for transient Cast/socket errors.

## Visual style

- Dark background (OLED-like) to suit a round bezel and save eye strain.
- Large type for title; one accent color for the active/volume elements.
- All hit targets ≥ ~44 px; keep interactive elements away from the extreme
  circular edge (corners are clipped on a round panel).

## LVGL notes

- LVGL **9.x**. Draw buffer(s) in **PSRAM**; double-buffer if RAM allows.
- Register the **encoder** as an LVGL input device (`LV_INDEV_TYPE_ENCODER`) so
  list navigation + focus come for free; register **touch** as
  `LV_INDEV_TYPE_POINTER` (CST816 → `lv_indev` read cb).
- Run `lv_timer_handler()` from `ui_task` only (LVGL is single-threaded).
- Use an arc (`lv_arc`) for the volume ring; a styled label stack for now-playing.

## Feedback & latency targets

| Event | Target |
|-------|--------|
| Knob → ring moves | immediate (optimistic, < 1 frame) |
| Knob → speaker volume actually changes | < ~200 ms |
| Press → play/pause reflected | < ~300 ms (optimistic icon flip, reconcile on status) |
| Device switch → now-playing shown | < ~500 ms (instant if session warm) |

## Haptics (stretch, DRV2605)

- Light click per volume detent; firmer click on min/max.
- Distinct effect on play vs pause.
- Confirmation buzz on device switch.
