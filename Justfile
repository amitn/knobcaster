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

# Physical UI smoke test: drive the device over serial, screenshot each step into
# uitest_shots/ + write EXPECTATIONS.md (an agent/human verifies the shots).
uitest:
    uv run python scripts/uitest.py {{ if PORT != "auto" { PORT } else { "" } }}
