# 09 — Hardware Reference: ELECROW CrowPanel 2.1" Rotary Display

Board: **ELECROW CrowPanel 2.1"-HMI ESP32 Rotary Display, 480×480**
Wiki: <https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html>

This is the **second supported board**, ESPHome-only (`esphome/elecrow.yaml`). It
shares the Cast core (`components/cast/`) with the Waveshare build via the
`cast_controller` external component — see [08-esphome-port.md](08-esphome-port.md).

## Pinout status: ⚠️ FROM VENDOR DOCS — not yet hardware-validated

The map below is from the Elecrow wiki plus Elecrow's **official ESPHome example**
([Elecrow-RD/CrowPanel-2.1inch-…](https://github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-IPS-Round-Touch-Knob-Screen))
and a community review ([espboards.dev](https://www.espboards.dev/blog/crowpanel-2-1-inch-rotary-display-review/)).
We have **no board on hand** — treat timings/polarities as a starting point.

## SoC / memory

| Item | Value |
|------|-------|
| MCU | **ESP32-S3** (dual-core LX7 @ up to 240 MHz) |
| Flash | 16 MB |
| PSRAM | 8 MB **octal (OPI)** — holds the LVGL/RGB framebuffer + TLS buffers |
| Connectivity | 2.4 GHz Wi-Fi b/g/n, BLE |

ESPHome board profile: `esp32-s3-devkitc-1` with `flash_size: 16MB` and
`psram: {mode: octal, speed: 80MHz}`.

## Display — ST7701S over 16-bit RGB parallel (480×480)

A round IPS panel driven over a **parallel RGB** bus (not QSPI like the Waveshare
SH8601). ESPHome's built-in `st7701s` platform handles it; the init sequence goes
over a small SPI bus, pixel data over the RGB bus.

| Signal | GPIO |
|--------|------|
| Init SPI: CS / SCK / SDA(MOSI) | 16 / 2 / 1 |
| DE / HSYNC / VSYNC / PCLK | 40 / 15 / 7 / 41 |
| Red R0–R4 | 46, 3, 8, 18, 17 |
| Green G0–G5 | 14, 13, 12, 11, 10, 9 |
| Blue B0–B4 | 5, 45, 48, 47, 21 |
| Backlight (LEDC, active-high) | 6 |

Timings (Elecrow official): hsync fp20/pw10/bp10, vsync fp8/pw10/bp10,
pclk 18 MHz **inverted**, `color_order: RGB`.

## PCF8574 I/O expander @ I2C 0x21

Several control lines hang off a PCF8574 rather than the ESP directly. Expander
pins idle **HIGH** at power-up, so the panel powers up before firmware runs.

| Expander pin | Function | ESPHome use |
|--------------|----------|-------------|
| P0 | Touch reset | `cst816` `reset_pin` |
| P2 | Touch INT | *unused* — INT can't be an ESP interrupt behind the expander, so touch **polls** |
| P3 | LCD power | held on (`switch: gpio`, `restore_mode: ALWAYS_ON`) |
| P4 | LCD reset | `st7701s` `reset_pin` (active-low) |
| P5 | Knob push button | `binary_sensor: gpio` (active-low; polled over I2C) |

## Touch / encoder / button

| Item | Detail |
|------|--------|
| Touch | **CST816** @ I2C 0x15, on SDA=38 / SCL=39 (shared with the PCF8574). Polled (`update_interval: 50ms`, `skip_probe: true`). |
| Encoder | **Standard quadrature**, A=42 B=4 (both pull-up). ESPHome's stock `rotary_encoder` decodes it — *unlike* the Waveshare's PCNT special case. Each detent calls `CastController::on_encoder_delta()`. |
| Push button | Real switch on PCF8574 P5. Short press opens the speaker list / selects the centered speaker. (The Waveshare board has no push switch.) |
| Haptics | **None** on this board. |

## How this differs from the Waveshare board

See the table in [`esphome/README.md`](../esphome/README.md) ("Two boards, one
component"). In short: RGB vs QSPI display, expander-gated touch/power, a normal
encoder + a real button, and no haptics — all handled by **compile-optional**
peripherals in `cast_controller` (`-DCAST_HAVE_{SH8601,HAPTICS,PCNT_ENCODER}`).

## Build / flash

```bash
just esphome-elecrow-config    # validate (fast)
just esphome-elecrow-build     # compile
just esphome-elecrow-run       # build + flash + log over USB
```

Web flashing (no toolchain): the build's **factory** image
`esphome/.esphome/build/cast-knob-elecrow/.pioenvs/cast-knob-elecrow/firmware.factory.bin`
can be uploaded at <https://web.esphome.io> (it bundles bootloader + partition
table + OTA data + app at offset 0).

## On-hardware bring-up TODO

Tune once a board is available (also noted in the YAML header):

- RGB porch / pclk timings + `pclk_inverted` (panel-specific).
- LCD-reset / touch-reset polarity (active-low assumed; flip if the panel stays blank).
- Encoder direction sign (swap `on_clockwise` / `on_anticlockwise` if inverted).
- LVGL `buffer_size` vs. tearing.
- Color order / `invert_colors` if colors look wrong.
