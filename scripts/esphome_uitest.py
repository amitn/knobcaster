#!/usr/bin/env python3
"""ESPHome UI smoke test: drive interactions over the serial console, screenshot
each step, and validate. The ESPHome counterpart of scripts/uitest.py.

cast_controller's serial console keys: S screenshot, +/- knob volume up/down,
p knob-press (next speaker), k play/pause, n/b next/prev track, m mute.

Each step sends a key, captures the framebuffer (--FBDUMP marker, like fbdump),
saves a PNG, and runs automatic checks. Volume is measured as blue arc-fill in a
ring mask (so the blue transport buttons don't count).

Usage: uv run python scripts/esphome_uitest.py [/dev/ttyACM0]
"""
import os
import sys
import time

import numpy as np
import serial
from PIL import Image

W, H = 360, 360
CX, CY = W / 2, H / 2
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "esphome_uitest_shots")

# Ring mask isolating the volume arc (radius ~170), excluding the inner buttons.
_yy, _xx = np.mgrid[0:H, 0:W]
_dist = np.sqrt((_xx - CX) ** 2 + (_yy - CY) ** 2)
RING = (_dist >= 150) & (_dist <= 178)


def capture(ser):
    ser.reset_input_buffer()
    ser.write(b"S")
    ser.flush()
    header = b"--FBDUMP %d %d RGB565--\n" % (W, H)
    need = W * H * 2
    buf = b""
    deadline = time.time() + 8
    while header not in buf:
        buf += ser.read(512)
        if time.time() > deadline:
            raise TimeoutError("no FBDUMP header (is the firmware flashed?)")
    payload = buf[buf.index(header) + len(header):]
    while len(payload) < need:
        c = ser.read(need - len(payload))
        if not c:
            raise IOError(f"short read {len(payload)}/{need}")
        payload += c
    arr = np.frombuffer(payload[:need], dtype="<u2").reshape(H, W)
    r = ((arr >> 11) & 0x1F) << 3
    g = ((arr >> 5) & 0x3F) << 2
    b = (arr & 0x1F) << 3
    return np.dstack([r, g, b]).astype("uint8")


def arc_fill(img):
    """Blue arc-indicator pixels within the ring mask ~ volume level."""
    b, r = img[:, :, 2].astype(int), img[:, :, 0].astype(int)
    return int(np.count_nonzero(RING & (b > 150) & (r < 90)))


# (key to send or None, step name)
STEPS = [
    (None, "00_baseline"),
    ("+", "01_vol_up_1"),
    ("+", "02_vol_up_2"),
    ("+", "03_vol_up_3"),
    ("-", "04_vol_down_1"),
    ("-", "05_vol_down_2"),
    ("p", "06_next_speaker"),
    ("k", "07_play_pause"),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    ser = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(0.5)

    results = []
    prev_fill = None
    prev_img = None
    for key, name in STEPS:
        if key:
            ser.write(key.encode())
            ser.flush()
            time.sleep(1.3)  # let the UI react
        img = capture(ser)
        Image.fromarray(img).save(os.path.join(OUT, name + ".png"))
        fill = arc_fill(img)

        checks = [("renders (non-blank)", int(img.max()) > 40)]
        if "vol_up" in name and prev_fill is not None:
            checks.append((f"arc grew ({prev_fill}->{fill})", fill >= prev_fill))
        if "vol_down" in name and prev_fill is not None:
            checks.append((f"arc shrank ({prev_fill}->{fill})", fill <= prev_fill))
        if "next_speaker" in name and prev_img is not None:
            changed = int(np.abs(img.astype(int) - prev_img.astype(int)).mean()) > 1
            checks.append(("screen changed", changed))

        prev_fill = fill
        prev_img = img
        ok = all(c[1] for c in checks)
        results.append((name, ok))
        flag = "PASS" if ok else "FAIL"
        print(f"  [{flag}] {name:18} arc_fill={fill:<5} " +
              " ".join(f"{'ok' if v else 'XX'}:{d}" for d, v in checks))

    ser.close()
    n = sum(1 for _, ok in results if ok)
    print(f"\n{n}/{len(results)} steps passed. Screenshots: {OUT}/")
    sys.exit(0 if n == len(results) else 1)


if __name__ == "__main__":
    main()
