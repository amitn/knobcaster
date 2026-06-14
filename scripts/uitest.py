#!/usr/bin/env python3
"""Physical UI smoke test.

Drives the device through the serial debug console (see components/bsp/fbdump.c),
captures a screenshot after each step into uitest_shots/, and writes
uitest_shots/EXPECTATIONS.md describing what each screenshot SHOULD show. An agent
(or human) then opens each PNG and verifies it matches its EXPECT line.

Requires: device flashed with the current firmware + connected; serial monitor
closed. Run via `just uitest` (or `python scripts/uitest.py [port]`).
"""
import glob
import os
import sys
import time

import numpy as np
import serial
from PIL import Image

W, H = 360, 360
OUT = "uitest_shots"

# (name, keys-to-send, expectation-for-the-resulting-screenshot)
STEPS = [
    ("01_now_playing", "",
     "NOW-PLAYING screen (no overlay): green Wi-Fi icon at top, a device name, a "
     "now-playing line (title/artist or a state like IDLE), three round blue "
     "transport buttons, a volume ring around the rim, and the bottom hint "
     "'press: speakers   swipe: device'."),
    ("02_open_list", "p",
     "DEVICE LIST overlay fills the screen: a vertical list of speaker names. The "
     "active speaker is the FIRST row, highlighted BLUE, prefixed '> ' and showing "
     "its cached state + volume%."),
    ("03_nav_down_3", "+++",
     "Same list. The BLUE highlight has moved DOWN ~3 rows away from the first "
     "row. The first row keeps its '> ' marker but is no longer highlighted."),
    ("04_nav_up_1", "-",
     "Same list. The BLUE highlight moved UP one row vs step 03 (net ~2 rows below "
     "the active/first row)."),
    ("05_select", "p",
     "List CLOSED — back on the NOW-PLAYING screen, now for the speaker that was "
     "highlighted in step 04 (that name shown at the top). May briefly say "
     "'connecting...'."),
    ("06_volume_up", "++++++",
     "NOW-PLAYING screen; the volume RING is visibly LARGER/fuller than step 05 "
     "(volume raised ~18%)."),
    ("07_volume_down", "----------",
     "NOW-PLAYING screen; the volume RING is visibly SMALLER than step 06 (volume "
     "lowered)."),
]


def find_port():
    for pat in ("/dev/ttyACM*", "/dev/cu.usbmodem*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    return "/dev/ttyACM0"


def capture(ser, path):
    ser.reset_input_buffer()
    ser.write(b"S")
    ser.flush()
    hdr = b"--FBDUMP %d %d RGB565--\n" % (W, H)
    need = W * H * 2
    buf = b""
    t0 = time.time()
    while hdr not in buf and time.time() - t0 < 8:
        buf += ser.read(256)
    if hdr not in buf:
        sys.exit("no framebuffer header — is the current firmware flashed?")
    pay = buf[buf.index(hdr) + len(hdr):]
    while len(pay) < need:
        chunk = ser.read(need - len(pay))
        if not chunk:
            sys.exit(f"short framebuffer read ({len(pay)}/{need})")
        pay += chunk
    arr = np.frombuffer(pay[:need], dtype="<u2").reshape(H, W)
    r = ((arr >> 11) & 0x1F) << 3
    g = ((arr >> 5) & 0x3F) << 2
    b = (arr & 0x1F) << 3
    Image.fromarray(np.dstack([r, g, b]).astype("uint8")).save(path)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    os.makedirs(OUT, exist_ok=True)
    ser = serial.Serial(port, 115200, timeout=2)
    time.sleep(0.3)

    report = ["# UI smoke test — verify each screenshot against its expectation",
              f"\nPort: `{port}`  ·  shots in `{OUT}/`\n"]
    for name, keys, expect in STEPS:
        for k in keys:
            ser.write(k.encode())
            ser.flush()
            time.sleep(0.25)
        time.sleep(0.4)
        png = os.path.join(OUT, name + ".png")
        capture(ser, png)
        report.append(f"## {name}\n- sent: `{keys or '(none)'}`\n- shot: `{png}`\n- EXPECT: {expect}\n")
        print(f"[{name}] sent {keys or '(none)'!r} -> {png}")
    ser.close()

    with open(os.path.join(OUT, "EXPECTATIONS.md"), "w") as f:
        f.write("\n".join(report))
    print(f"\nWrote {OUT}/EXPECTATIONS.md. Review each {OUT}/*.png against its EXPECT line.")


if __name__ == "__main__":
    main()
