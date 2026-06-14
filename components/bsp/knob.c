// Rotary encoder via PCNT (x4 quadrature) + push button via GPIO ISR.
#include "knob.h"
#include "board_pins.h"

#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "knob";

// ~4 PCNT counts per physical detent on this encoder.
#define COUNTS_PER_DETENT 4

static pcnt_unit_handle_t s_pcnt;
static int                s_last_count;

#define LONG_PRESS_US 600000   // hold >= 600ms => long press
#define DEBOUNCE_US   20000

static volatile bool      s_pressed;        // pending short press
static volatile bool      s_long_pressed;   // pending long press
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

void knob_init(void)
{
    // --- Encoder: PCNT unit with both channels for x4 decoding ---
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 1000,
        .low_limit  = -1000,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt));

    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = 1000 };
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
    int count = 0;
    if (pcnt_unit_get_count(s_pcnt, &count) != ESP_OK) return 0;
    int diff = count - s_last_count;
    int detents = diff / COUNTS_PER_DETENT;
    if (detents != 0) {
        // Keep the sub-detent remainder so slow turns aren't lost.
        s_last_count += detents * COUNTS_PER_DETENT;
    }
    return detents;
}

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
