// Waveshare ESP32-S3-Knob-Touch-LCD-1.8 pin map.
// Confirmed against the EmbeddedWizard + BlueKnob BSPs (see docs/01-hardware.md).
#pragma once

#include "driver/gpio.h"

// Display: SH8601 over QSPI on SPI2_HOST
#define BOARD_LCD_HOST          SPI2_HOST
#define BOARD_LCD_H_RES         360
#define BOARD_LCD_V_RES         360
#define BOARD_PIN_LCD_CS        14
#define BOARD_PIN_LCD_PCLK      13
#define BOARD_PIN_LCD_DATA0     15
#define BOARD_PIN_LCD_DATA1     16
#define BOARD_PIN_LCD_DATA2     17
#define BOARD_PIN_LCD_DATA3     18
#define BOARD_PIN_LCD_RST       21
#define BOARD_PIN_LCD_BL        47   // backlight (active-high)

// Touch: CST816 over I2C_NUM_0
#define BOARD_TOUCH_I2C_PORT    I2C_NUM_0
#define BOARD_PIN_TOUCH_SDA     11
#define BOARD_PIN_TOUCH_SCL     12
#define BOARD_PIN_TOUCH_INT     9
#define BOARD_PIN_TOUCH_RST     10
#define BOARD_TOUCH_ADDR        0x15

// Rotary encoder + push button
#define BOARD_PIN_ENC_A         8
#define BOARD_PIN_ENC_B         7
#define BOARD_PIN_BTN           0    // BOOT/strapping, active-low

// Haptics (stretch)
#define BOARD_DRV2605_ADDR      0x5A
