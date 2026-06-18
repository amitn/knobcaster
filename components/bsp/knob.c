// Rotary encoder via PCNT + push button via GPIO ISR.
#include "knob.h"
#include "board_pins.h"
#include "haptics.h"

#include "freertos/FreeRTOS.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "knob";

static pcnt_unit_handle_t s_pcnt;

// This board's encoder pulses the PCNT count to +/-1 per detent and returns to 0
// (it does NOT accumulate). We count ONE detent per departure from 0 (sign =
// direction) and only re-arm when the count returns to 0 — this rejects the
// glitchy mid-detent reversals that otherwise made one direction "chunky". The
// state machine runs in the PCNT watch-point ISR (values -1/0/+1), so the
// encoder burns zero idle CPU and wakes only on a real counter change.
static volatile int  s_enc_accum;
static volatile bool s_armed = true;   // false after a detent until count hits 0

#define LONG_PRESS_US 600000   // hold >= 600ms => long press
#define DEBOUNCE_US   20000

static volatile bool      s_pressed;        // pending short press
static volatile bool      s_long_pressed;   // pending long press
static volatile int       s_injected_delta; // detents injected via serial debug
static volatile int64_t   s_down_us;        // press-down timestamp (0 = up)
static volatile int64_t   s_last_edge_us;   // debounce timestamp

// Active-low button on an ANYEDGE interrupt: time the press and classify it as
// short or long on release.
static void IRAM_ATTR button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_edge_us < DEBOUNCE_US) return;
    s_last_edge_us = now;

    if (gpio_get_level(BOARD_PIN_BTN) == 0) {   // pressed
        s_down_us = now;
    } else if (s_down_us != 0) {                // released
        if (now - s_down_us >= LONG_PRESS_US) s_long_pressed = true;
        else                                  s_pressed = true;
        s_down_us = 0;
    }
}

// PCNT watch-point callback (ISR context). Fires when the count reaches -1, 0,
// or +1: a departure from 0 is one detent (re-arm only back at 0). Keep it
// minimal — bump the accumulator and kick haptics via its ISR-safe path; the
// I2C write is deferred to the haptics worker task.
static bool enc_on_reach(pcnt_unit_handle_t unit,
                         const pcnt_watch_event_data_t *edata, void *ctx)
{
    BaseType_t hp = pdFALSE;
    int v = edata->watch_point_value;
    if (v == 0) {
        s_armed = true;
    } else if (s_armed) {
        s_enc_accum += (v > 0) ? 1 : -1;   // sign = direction
        s_armed = false;
        haptics_click_from_isr(&hp);       // tactile "detent" on a smooth encoder
    }
    return hp == pdTRUE;                    // yield if the haptics task woke
}

void knob_init(void)
{
    // --- Encoder: PCNT unit with both channels for x4 decoding ---
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 1000,
        .low_limit  = -1000,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt));

    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = 5000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt, &filter));

    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num  = BOARD_PIN_ENC_A,
        .level_gpio_num = BOARD_PIN_ENC_B,
    };
    pcnt_channel_handle_t chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &chan_a_cfg, &chan_a));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num  = BOARD_PIN_ENC_B,
        .level_gpio_num = BOARD_PIN_ENC_A,
    };
    pcnt_channel_handle_t chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &chan_b_cfg, &chan_b));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Mechanical encoders need pull-ups; PCNT doesn't enable them, so floating
    // A/B lines would never produce clean counts.
    gpio_set_pull_mode(BOARD_PIN_ENC_A, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BOARD_PIN_ENC_B, GPIO_PULLUP_ONLY);

    // Watch the three values the per-detent pulse touches; the callback decodes
    // detents on departure-from-0. Must be registered before the unit is enabled.
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt, -1));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt, 0));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt, 1));
    pcnt_event_callbacks_t cbs = { .on_reach = enc_on_reach };
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(s_pcnt, &cbs, NULL));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt));

    // --- Button: GPIO0, active-low, edge interrupt ---
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOARD_PIN_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&btn);
    esp_err_t e = gpio_install_isr_service(0);   // may already be installed (touch)
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(e);
    gpio_isr_handler_add(BOARD_PIN_BTN, button_isr, NULL);

    ESP_LOGI(TAG, "knob ready (A=%d B=%d btn=%d)",
             BOARD_PIN_ENC_A, BOARD_PIN_ENC_B, BOARD_PIN_BTN);
}

int knob_take_delta(void)
{
    int d = s_enc_accum;        s_enc_accum = 0;
    int inj = s_injected_delta; s_injected_delta = 0;
    return d + inj;
}

void knob_inject_delta(int detents) { s_injected_delta += detents; }
void knob_inject_press(void)        { s_pressed = true; }
void knob_inject_long_press(void)   { s_long_pressed = true; }

bool knob_take_pressed(void)
{
    if (!s_pressed) return false;
    s_pressed = false;
    return true;
}

bool knob_take_long_pressed(void)
{
    if (!s_long_pressed) return false;
    s_long_pressed = false;
    return true;
}
