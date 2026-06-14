// Rotary encoder (PCNT quadrature on A=GPIO8, B=GPIO7) + push button (GPIO0).
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the encoder (PCNT) and button (GPIO ISR). Call once at startup.
void knob_init(void);

// Consume the encoder movement since the last call, in detents
// (positive = clockwise). Returns 0 if the knob hasn't moved.
int knob_take_delta(void);

// Consume a pending short button press (released before the long-press
// threshold). Returns true once per press.
bool knob_take_pressed(void);

// Consume a pending long press (held past ~600 ms). Returns true once per press.
bool knob_take_long_pressed(void);

// --- debug input injection (serial console emulates the knob) -----------------
void knob_inject_delta(int detents);   // +CW / -CCW, as if rotated
void knob_inject_press(void);          // short press
void knob_inject_long_press(void);     // long press

#ifdef __cplusplus
}
#endif
