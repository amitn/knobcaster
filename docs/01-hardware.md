# 01 — Hardware Reference

Board: **Waveshare ESP32-S3-Knob-Touch-LCD-1.8**
Wiki: <https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8>

## Pinout status: ✅ CONFIRMED

The GPIO map below is corroborated by **two independent ESP-IDF BSPs that target
this exact board** — every pin, including the push-button, is now confirmed:

- [EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN](https://github.com/EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN)
  → `main/TargetSpecific/user_config.h` (pins), `ew_bsp_display.c` (panel),
  `ew_bsp_inout.c` (button `GPIO0`)
- [joshuacant/BlueKnob](https://github.com/joshuacant/BlueKnob) (same board; `esp_lcd_sh8601` driver)

> ⚠️ **Display controller correction.** The Waveshare wiki and several community
> threads call this an **ST77916** panel. **Both** working ESP-IDF BSPs for this
> exact board instead drive it as an **SH8601** (`esp_lcd_sh8601`, QSPI) with a
> specific init sequence. We use **SH8601**. If a board revision ever ships a real
> ST77916, revisit this.

## SoC / memory

| Item | Value |
|------|-------|
| Main MCU | **ESP32-S3R8** (dual-core LX7 @ up to 240 MHz) — *we program this* |
| Secondary MCU | ESP32-U4WDH — runs stock **Bluetooth A2DP/AVRC audio** firmware (not unused; we don't touch it) |
| Flash | 16 MB (QIO) |
| PSRAM | 8 MB **octal (OPI)** — required; LVGL buffers + TLS live here |
| Connectivity | 2.4 GHz Wi-Fi b/g/n, BT5 (BLE) + BT Classic |

> PlatformIO board profile: custom `boards/esp32s3-knob.json` (16 MB flash, 8 MB
> OPI PSRAM). PSRAM set octal via `CONFIG_SPIRAM_MODE_OCT` in `sdkconfig.defaults`.

### USB / which chip is which (important for flashing)

Two distinct serial devices appear, one per MCU — **flash the S3, never the CH340**:

| Enumerates as | VID:PID | MCU | Use |
|---------------|---------|-----|-----|
| **USB JTAG/serial debug unit** | `303a:1001` | ESP32-S3 (ours) | flash + monitor (`/dev/ttyACM*`) |
| **USB-SERIAL CH340** | `1a86:7523` | secondary ESP32 (BT audio) | *wrong target* — leave alone |

Selecting the CH340 in a flasher (e.g. ESP Web Tools) talks to the secondary
ESP32 (a plain ESP32 running Bluetooth A2DP), which fails as "board not
supported" and shows a BT/`A2DP`/`AVRC` boot log. The S3's native USB may need a
specific USB-C port / a USB-select switch on the board, or BOOT+RESET to force
download mode, before it enumerates.

## Display — SH8601 (QSPI)

| Property | Value |
|----------|-------|
| Panel | 1.8" round, **360 × 360**, 16-bit color (RGB565) |
| Controller | **SH8601** (QSPI AMOLED-style driver) |
| Interface | **QSPI** on `SPI2_HOST`, `bits_per_pixel=16`, RGB order |
| ESP-IDF driver | `espressif/esp_lcd_sh8601` + board init cmds (from EmbeddedWizard BSP) + `esp_lvgl_port` |
| Backlight | GPIO47, PWM (LEDC) |

> Panel IO via `SH8601_PANEL_IO_QSPI_CONFIG(CS, ...)`; panel via
> `esp_lcd_new_panel_sh8601()` with `sh8601_vendor_config_t{ .flags.use_qspi_interface = 1 }`
> and the ~190-entry init-command table copied from `ew_bsp_display.c`.

## Touch — CST816 (I2C)

| Property | Value |
|----------|-------|
| Controller | **CST816 / CST816S** capacitive |
| Bus | `I2C_NUM_0`, **address `0x15`** |
| ESP-IDF driver | `esp_lcd_touch_cst816s` (managed component) |

## Input — rotary encoder + button

- Rotary **encoder** (A/B quadrature) — primary control (volume / list scroll).
  Confirmed true quadrature in the EmbeddedWizard BSP (`ew_bsp_inout.c`).
- **Push** action on the knob — select / play-pause. **GPIO0** (BOOT/strapping),
  active-low; EmbeddedWizard `ew_bsp_inout.c` reads it with an ANYEDGE GPIO ISR.
- ESP-IDF: decode the encoder with the hardware **PCNT** peripheral
  (`driver/pulse_cnt.h`); button via GPIO ISR or the `espressif/button` component.

## Confirmed GPIO map

### Display (SH8601, QSPI on SPI2_HOST)
| Signal | GPIO |
|--------|------|
| CS | 14 |
| SCLK / PCLK | 13 |
| D0 (IO0) | 15 |
| D1 (IO1) | 16 |
| D2 (IO2) | 17 |
| D3 (IO3) | 18 |
| RST | 21 |
| Backlight (BL) | 47 |

### Touch (CST816, I2C_NUM_0, addr 0x15)
| Signal | GPIO |
|--------|------|
| SDA | 11 |
| SCL | 12 |
| INT | 9 |
| RST | 10 |

### Rotary encoder / button
| Signal | GPIO |
|--------|------|
| Encoder A | 8 |
| Encoder B | 7 |
| Push button | 0 (BOOT/strapping, active-low) |

> Earlier drafts flagged a "conflict" where I2S/SD seemed to share the display
> pins — that was a garbled extraction. The display owns GPIO 13–18 on `SPI2_HOST`;
> audio/SD are irrelevant to the MVP and not used here.

## Other onboard peripherals (not needed for MVP)

| Peripheral | Part | Bus / addr | MVP use |
|------------|------|------------|---------|
| Haptics | DRV2605 | I2C `0x5A` | stretch: knob detent feedback |
| Audio DAC | PCM5100A | I2S | — (stretch: audio-reactive UI) |
| Microphone | digital MEMS | I2S/PDM | — |
| Storage | microSD (TF) | SDMMC | optional asset/config storage |
| Power | Li-ion charge mgmt | — | battery operation |

## Implications for firmware

- **PSRAM-first allocation.** LVGL draw buffers + the TLS read buffer go to PSRAM
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`). EmbeddedWizard uses a 360×36
  (1/10th screen) partial draw buffer — a good starting size.
- **QSPI display** uses the `esp_lcd_sh8601` driver with the board init-command
  table from EmbeddedWizard `ew_bsp_display.c` (copied into `hal/display`).
- **USB Serial/JTAG console** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`) carries logs
  over the native USB port.
