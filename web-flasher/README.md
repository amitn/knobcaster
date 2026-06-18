# Web flasher (ESP Web Tools)

A browser page that flashes the firmware to a fresh Waveshare ESP32-S3-Knob over
USB (WebSerial) — no PlatformIO/esptool install needed. This is for the **initial**
flash; afterwards the device updates itself over the air (see `components/ota`).

- `index.html` — the install page (loads [ESP Web Tools](https://esphome.github.io/esp-web-tools/) from a CDN).
- `manifest.json` — describes the image; `__VERSION__` and the merged binary are
  filled in by CI. `chipFamily` is `ESP32-S3`, a single merged part at offset `0`.

## How it ships

The **Web Flasher** workflow (`.github/workflows/web-flasher.yml`) runs on every
`v*` tag: it builds the firmware, merges the flash parts (bootloader + partition
table + otadata + app) into one `firmware-merged.bin` with `esptool merge-bin`,
substitutes the tag into `manifest.json`, and deploys `index.html` +
`manifest.json` + `firmware-merged.bin` to **GitHub Pages** (same origin, so no
CORS). The flasher then lives at `https://amitn.github.io/knobcaster/`.

## Test it locally (no deploy)

```bash
just web-flash-local        # builds, merges, serves http://localhost:8000
```

Open the printed `http://localhost:<port>` in Chrome/Edge — WebSerial treats
localhost as a secure context, so flashing works over plain HTTP, exactly like the
deployed page. On WSL, detach the board from `usbipd` first so the **Windows**
browser sees the COM port (pick the *ESP32-S3* / "USB JTAG/serial debug unit"
port, not the CH340 bridge).

## One-time setup

GitHub Pages must be enabled with **Settings → Pages → Source: GitHub Actions**,
or the deploy step fails. WebSerial needs Chrome/Edge on desktop over HTTPS
(Pages is HTTPS).
