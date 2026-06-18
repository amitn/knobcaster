// On-board DRV2605 haptic driver (I2C 0x5A on the shared bus). This knob's
// encoder is mechanically smooth, so a short haptic pulse per detent gives it a
// tactile "click" feel.
#pragma once

#include "freertos/FreeRTOS.h"

// Initialise the DRV2605 and start the worker task. Safe to call even if the
// chip is absent (clicks become no-ops). Call after board_i2c is available.
void haptics_start(void);

// Fire one short click. Non-blocking and safe from any task; the actual I2C
// write happens on the haptics worker. No-op if haptics failed to init.
void haptics_click(void);

// ISR-context variant of haptics_click() (e.g. from the encoder PCNT callback).
// Sets *hp_woken to pdTRUE if a higher-priority task was woken, so the caller
// can request a context switch on ISR exit. No-op if haptics failed to init.
void haptics_click_from_isr(BaseType_t *hp_woken);
