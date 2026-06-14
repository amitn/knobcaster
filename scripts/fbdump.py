#!/usr/bin/env python3
"""Capture the device LCD framebuffer over serial and save it as a PNG.

Sends 'S' to the firmware (see components/bsp/fbdump.c), waits for the
`--FBDUMP W H RGB565--` header, reads W*H*2 raw little-endian RGB565 bytes, and
writes a PNG. Usage: python scripts/fbdump.py [out.png] [port]
"""
import glob
import os
import sys
import time

import numpy as np
import serial
from PIL import Image

W, H = 360, 360


def find_port():
    if os.environ.get("FBDUMP_PORT"):
        return os.environ["FBDUMP_PORT"]
    for pat in ("/dev/ttyACM*", "/dev/cu.usbmodem*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    return "/dev/ttyACM0"


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "screen.png"
    port = sys.argv[2] if len(sys.argv) > 2 else find_port()

    ser = serial.Serial(port, 115200, timeout=2)
    ser.reset_input_buffer()
    ser.write(b"S")
    ser.flush()

    header = b"--FBDUMP %d %d RGB565--\n" % (W, H)
    need = W * H * 2
    buf = b""
    deadline = time.time() + 10
    while header not in buf:
        chunk = ser.read(256)
        buf += chunk
        if time.time() > deadline:
            sys.exit(f"timeout waiting for framebuffer header on {port}")

    payload = buf[buf.index(header) + len(header):]
    while len(payload) < need:
        chunk = ser.read(need - len(payload))
        if not chunk:
            sys.exit(f"short read: got {len(payload)}/{need} bytes")
        payload += chunk
    ser.close()

    arr = np.frombuffer(payload[:need], dtype="<u2").reshape(H, W)
    r = ((arr >> 11) & 0x1F) << 3
    g = ((arr >> 5) & 0x3F) << 2
    b = (arr & 0x1F) << 3
    Image.fromarray(np.dstack([r, g, b]).astype("uint8")).save(out)
    print(f"saved {out} ({W}x{H}) from {port}")


if __name__ == "__main__":
    main()
