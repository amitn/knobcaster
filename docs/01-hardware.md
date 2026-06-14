# 01 — Hardware Reference

Board: **Waveshare ESP32-S3-Knob-Touch-LCD-1.8**
Wiki: <https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8>

## Pinout status: ✅ CONFIRMED

The GPIO map below is corroborated by **two independent ESP-IDF BSPs that target
this exact board**:

- [EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN](https://github.com/EmbeddedWizardGUI/ESP32-S3-Knob-Touch-LCD-1.8-EN)
  → `main/TargetSpecific/user_config.h` (authoritative pin defines)
- [joshuacant/BlueKnob](https://github.com/joshuacant/BlueKnob) (same board, BLE remote)

The only item still **TBD** is the knob **push-button** GPIO (not defined in the
EmbeddedWizard `user_config.h`; candidate is the BOOT/strapping `GPIO0`). Confirm
on hardware before wiring the press action.

## SoC / memory

| Item | Value |
|------|-------|
| Main MCU | **ESP32-S3R8** (dual-core LX7 @ up to 240 MHz) — *we program this* |
| Secondary MCU | ESP32-U4WDH — USB-UART bridge / co-processor (**unused** by us) |
| Flash | 16 MB (QIO) |
| PSRAM | 8 MB **octal (OPI)** — required; LVGL buffers + TLS live here |
| Connectivity | 2.4 GHz Wi-Fi b/g/n, BT5 (BLE) + BT Classic |

> PlatformIO board profile: `esp32-s3-devkitc-1-n16r8` (16 MB flash, 8 MB OPI
> PSRAM). Matches the S3R8 memory config; PSRAM set octal via
> `CONFIG_SPIRAM_MODE_OCT` in `sdkconfig.defaults`.

## Display — ST77916 (QSPI)

| Property | Value |
|----------|-------|
| Panel | 1.8" round IPS, **360 × 360**, 262K colors |
| Controller | **ST77916** |
| Interface | **QSPI** on `SPI2_HOST` |
| ESP-IDF driver | `esp_lcd_st77916` (managed component) + `esp_lvgl_port` |

## Touch — CST816 (I2C)

| Property | Value |
|----------|-------|
| Controller | **CST816 / CST816S** capacitive |
| Bus | `I2C_NUM_0`, **address `0x15`** |
| ESP-IDF driver | `esp_lcd_touch_cst816s` (managed component) |

## Input — rotary encoder + button

- Rotary **encoder** (A/B quadrature) — primary control (volume / list scroll).
  Confirmed true quadrature in the EmbeddedWizard BSP (`ew_bsp_inout.c`).
- **Push** action on the knob — select / play-pause. ⚠️ Pin TBD (see above).
- ESP-IDF: decode with the hardware **PCNT** peripheral (`driver/pulse_cnt.h`)
  or the `espressif/knob` + `espressif/button` components.

## Confirmed GPIO map

### Display (ST77916, QSPI on SPI2_HOST)
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
| Push button | ⚠️ TBD (candidate: GPIO0 / BOOT) |

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
- **QSPI display** needs the `esp_lcd_st77916` driver in QSPI mode; reuse the
  EmbeddedWizard `ew_bsp_display.c` init sequence as a reference.
- **USB Serial/JTAG console** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`) carries logs
  over the native USB port.
