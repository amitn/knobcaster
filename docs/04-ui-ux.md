# 04 — UI / UX

Surface: a **360 × 360 round** touch LCD, a **rotary encoder with push**, and
(stretch) **haptics**. Two primary gestures define the experience:

- **Dial (rotate)** → **volume** of the active device.
- **Swipe left / right** → **select device** (cycle through discovered speakers).

Everything else is secondary.

## Interaction model

| Gesture | Action |
|---------|--------|
| **Rotate dial** | Adjust **volume** of the active device (optimistic, instant ring) |
| **Swipe left / right** | **Select device** — cycle to prev/next discovered speaker |
| **Press dial** | **Play / Pause** toggle on the active device |
| **Long-press dial** | **Mute / unmute** the active device |
| **Tap left/right edges** | Previous / Next track |
| **Tap center** | Open **device list** overlay (jump directly to any device) |
| **Tap a device in list** | Make it active, return to now-playing |

The two primaries are deliberately split across the two input surfaces — the
**dial owns volume**, **swipe owns device selection** — so they never compete.
The device list (tap center) is just a faster path to the same selection that
swiping cycles through.

> Single source of truth: the UI only renders an `AppState` snapshot and emits
> *intents*; it never talks to the network directly (see
> [02-architecture.md](02-architecture.md#concurrency-model)).

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
