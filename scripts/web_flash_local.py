#!/usr/bin/env python3
"""Serve the web flasher from http://localhost for testing — no GitHub Pages deploy.

Assembles the same site the CI publishes (index.html + versioned manifest + a
merged firmware image) into .pio/web-flasher-site and serves it. WebSerial treats
localhost as a secure context, so flashing works over plain HTTP.

Open the printed URL in Chrome/Edge on the machine the board is plugged into. On
WSL, detach the board from usbipd first so the Windows browser sees the COM port.

Usage: uv run python scripts/web_flash_local.py [PORT]   (default 8000)
"""
import functools
import http.server
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, ".pio", "web-flasher-site")
BUILD = os.path.join(ROOT, ".pio", "build", "knob")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000


def version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=ROOT, text=True).strip()
    except Exception:
        return "dev"


def main():
    if not os.path.isfile(os.path.join(BUILD, "firmware.bin")):
        sys.exit("No build found — run `just build` first.")

    if os.path.isdir(SITE):
        shutil.rmtree(SITE)
    os.makedirs(SITE)

    ver = version()
    shutil.copy(os.path.join(ROOT, "web-flasher", "index.html"), SITE)
    with open(os.path.join(ROOT, "web-flasher", "manifest.json")) as f:
        manifest = f.read().replace("__VERSION__", ver)
    with open(os.path.join(SITE, "manifest.json"), "w") as f:
        f.write(manifest)

    # Merge bootloader + partition table + otadata + app into one image at 0x0.
    subprocess.check_call([
        "esptool", "--chip", "esp32s3", "merge-bin",
        "-o", os.path.join(SITE, "firmware-merged.bin"),
        "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
        "0x0", os.path.join(BUILD, "bootloader.bin"),
        "0x8000", os.path.join(BUILD, "partitions.bin"),
        "0xe000", os.path.join(BUILD, "ota_data_initial.bin"),
        "0x10000", os.path.join(BUILD, "firmware.bin"),
    ])

    print(f"\n→ open http://localhost:{PORT} in Chrome/Edge  (version {ver})")
    print("  (Ctrl-C to stop)\n")
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=SITE)
    http.server.ThreadingHTTPServer(("0.0.0.0", PORT), handler).serve_forever()


if __name__ == "__main__":
    main()
