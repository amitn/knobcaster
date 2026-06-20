# Justfile — task runner for the ESP32-S3 Knob Cast controller.
#
# All firmware tasks run PlatformIO *inside the uv-managed virtualenv* via
# `uv run pio ...`. Run `just setup` once, then `just build`, `just flash`, etc.

set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

# Serial port for the board. Override per-invocation, e.g.
#   just PORT=/dev/ttyACM0 flash
PORT := "auto"
BAUD := "115200"

# Show available recipes (default target).
default:
    @just --list

# ----------------------------------------------------------------------------
# Environment bootstrap
# ----------------------------------------------------------------------------

# Create the uv venv (.venv) and install the pinned PlatformIO into it.
setup:
    uv sync
    uv run pio --version
    @echo "✅ Toolchain ready. PlatformIO runs inside $(uv run python -c 'import sys; print(sys.prefix)')"

# Install/refresh PlatformIO platform + project library dependencies.
deps:
    uv run pio pkg install

# Print the resolved toolchain + platform versions.
doctor:
    uv run python --version
    uv run pio --version
    uv run pio system info

# ----------------------------------------------------------------------------
# Build / flash / monitor
# ----------------------------------------------------------------------------

# Compile the firmware.
build:
    uv run pio run

# Upload firmware to the board.
upload:
    uv run pio run -t upload {{ if PORT != "auto" { "--upload-port " + PORT } else { "" } }}

# Build + upload in one step.
flash: build upload

# Open the serial monitor.
monitor:
    uv run pio device monitor -b {{BAUD}} {{ if PORT != "auto" { "-p " + PORT } else { "" } }}

# Build, upload, then immediately attach the monitor.
dev: build upload monitor

# List detected serial devices (helps find PORT).
ports:
    uv run pio device list

# Capture the LCD framebuffer over serial to a PNG (default screen.png).
# Sends 'S' to the firmware; needs the serial monitor closed.
shot out="screen.png":
    uv run python scripts/fbdump.py {{out}} {{ if PORT != "auto" { PORT } else { "" } }}

# ----------------------------------------------------------------------------
# Maintenance
# ----------------------------------------------------------------------------

# Remove PlatformIO build artifacts.
clean:
    uv run pio run -t clean

# Nuke build dirs and the venv for a fully clean slate.
distclean: clean
    rm -rf .pio .venv

# Static analysis over the firmware sources.
check:
    uv run pio check

# Run host-native unit tests (pure parsers; no hardware).
test:
    uv run pio test -e native

# Regenerate the embedded Hebrew/Latin TTF subset (components/ui/fonts/).
gen-font:
    uv run python scripts/gen_font.py

# Regenerate the committed ESPHome UI font subset (esphome/fonts/DejaVuSans-knob.ttf).
gen-esphome-font:
    uvx --from esphome --with fonttools python scripts/gen_esphome_font.py

# Physical UI smoke test: drive the device over serial, screenshot each step into
# uitest_shots/ + write EXPECTATIONS.md (an agent/human verifies the shots).
uitest:
    uv run python scripts/uitest.py {{ if PORT != "auto" { PORT } else { "" } }}

# Serve the web flasher locally for testing (same page as GitHub Pages, but from
# http://localhost). WebSerial treats localhost as a secure context, so flashing
# works without HTTPS — no deploy needed. Open the URL in Chrome/Edge on the
# machine the board is plugged into (on WSL: detach from usbipd so Windows sees
# the COM port). Builds first so the merged image is current.
web-flash-local port="8000": build
    uv run python scripts/web_flash_local.py {{port}}

# Cut the next release: bump the latest vX.Y.Z tag and push it (triggers the
# Release + Web Flasher workflows). Default bumps patch; pass minor/major.
# Preview without tagging:  just release patch --dry-run
release level="patch" *flags="":
    uv run python scripts/release.py {{level}} {{flags}}

# Build ALL THREE firmwares and collect web-flashable FACTORY images into dist/
# (plus the vanilla OTA firmware.bin). These are the assets the Release workflow
# attaches to a GitHub Release; each *.factory.bin can be flashed from
# https://web.esphome.io ("install from file"). Run locally to reproduce them.
# `version` stamps the vanilla image (version.txt -> esp_app_get_description()).
dist-all version="dev": (dist-vanilla version) dist-waveshare dist-elecrow
    @echo "==> dist/ (release assets):"
    ls -la dist

# One target at a time -> dist/ (used by the Release matrix so the three builds
# run on separate GitHub runners in parallel). Each is self-contained.

# Vanilla ESP-IDF: OTA firmware.bin + a merged, web-flashable factory image.
dist-vanilla version="dev":
    echo "{{version}}" > version.txt
    mkdir -p dist
    just build
    cp .pio/build/knob/firmware.bin dist/firmware.bin
    uv run esptool --chip esp32s3 merge-bin -o dist/cast-knob-vanilla.factory.bin --flash-mode dio --flash-freq 80m --flash-size 16MB 0x0 .pio/build/knob/bootloader.bin 0x8000 .pio/build/knob/partitions.bin 0xe000 .pio/build/knob/ota_data_initial.bin 0x10000 .pio/build/knob/firmware.bin

# ESPHome Waveshare (cast-knob): factory image (ESPHome emits it directly).
dist-waveshare:
    mkdir -p dist
    just esphome-build
    cp esphome/.esphome/build/cast-knob/.pioenvs/cast-knob/firmware.factory.bin dist/cast-knob-waveshare.factory.bin

# ESPHome Elecrow (cast-knob-elecrow): factory image.
dist-elecrow:
    mkdir -p dist
    just esphome-elecrow-build
    cp esphome/.esphome/build/cast-knob-elecrow/.pioenvs/cast-knob-elecrow/firmware.factory.bin dist/cast-knob-elecrow.factory.bin

# --- ESPHome port (branch esphome-port) -------------------------------------

# Validate the ESPHome YAML (fast; no toolchain download).
esphome-config:
    uvx esphome config esphome/knob.yaml

# Compile the ESPHome firmware (slow first time — downloads the ESP-IDF toolchain).
esphome-build:
    uvx esphome compile esphome/knob.yaml

# Build + flash + log over USB (needs the board on this machine).
esphome-run:
    uvx esphome run esphome/knob.yaml

# --- ESPHome: ELECROW CrowPanel 2.1" Rotary Display (480x480) ----------------
# Second board target; shares components/cast/ + cast_controller with knob.yaml.

# Validate the Elecrow YAML (fast; no toolchain download).
esphome-elecrow-config:
    uvx esphome config esphome/elecrow.yaml

# Compile the Elecrow firmware (slow first time — downloads the ESP-IDF toolchain).
esphome-elecrow-build:
    uvx esphome compile esphome/elecrow.yaml

# Build + flash + log over USB (needs the Elecrow board on this machine).
esphome-elecrow-run:
    uvx esphome run esphome/elecrow.yaml


# ESPHome UI smoke test: drive interactions over serial, screenshot + validate.
esphome-uitest:
    uv run python scripts/esphome_uitest.py
