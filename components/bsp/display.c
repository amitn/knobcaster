// SH8601 360x360 QSPI display + LVGL (esp_lvgl_port) bring-up.
//
// The init-command table below is the board-specific panel tuning copied from
// the EmbeddedWizard BSP for this exact board (ew_bsp_display.c). The board's
// controller is SH8601 (not ST77916 as the wiki claims) -- see docs/01-hardware.md.
#include "display.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel;    // captured in display_init() for sleep/wake
static volatile bool          s_asleep;   // false at boot (panel is on)

// Init-command table lives in the shared header (single source of truth,
// also used by esphome/components/sh8601/). See sh8601_init_cmds.h.
#include "sh8601_init_cmds.h"

void display_backlight(bool on)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_PIN_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_PIN_LCD_BL, on ? 1 : 0);
}

// Screen power. Blank the panel + backlight (LVGL keeps running so the knob,
// button, and touch still wake it; the Cast net task keeps the screen content
// current underneath). Idempotent.
void display_sleep(void)
{
    if (s_asleep) return;
    display_backlight(false);                     // kill the glow first
    lvgl_port_lock(0);
    esp_lcd_panel_disp_on_off(s_panel, false);    // panel off after it's dark
    lvgl_port_unlock();
    s_asleep = true;
    ESP_LOGI(TAG, "screen sleep");
}

void display_wake(void)
{
    if (!s_asleep) return;
    lvgl_port_lock(0);
    esp_lcd_panel_disp_on_off(s_panel, true);     // panel on before it lights up
    lvgl_port_unlock();
    display_backlight(true);
    s_asleep = false;
    ESP_LOGI(TAG, "screen wake");
}

bool display_is_asleep(void) { return s_asleep; }

lv_display_t *display_init(void)
{
    ESP_LOGI(TAG, "init SH8601 QSPI panel (%dx%d)", BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    const spi_bus_config_t buscfg = {
        .data0_io_num = BOARD_PIN_LCD_DATA0,
        .data1_io_num = BOARD_PIN_LCD_DATA1,
        .data2_io_num = BOARD_PIN_LCD_DATA2,
        .data3_io_num = BOARD_PIN_LCD_DATA3,
        .sclk_io_num  = BOARD_PIN_LCD_PCLK,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg =
        SH8601_PANEL_IO_QSPI_CONFIG(BOARD_PIN_LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_cfg, &io));

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = sh8601_init_cmds,
        .init_cmds_size = sizeof(sh8601_init_cmds) / sizeof(sh8601_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor_config,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel));
    s_panel = panel;   // keep for display_sleep()/display_wake()
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    display_backlight(true);

    const lvgl_port_cfg_t port_cfg = {
        .task_priority   = 4,
        .task_stack      = 6144,
        .task_affinity   = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = io,
        .panel_handle = panel,
        .buffer_size  = BOARD_LCD_H_RES * 40,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .buff_spiram = false, .swap_bytes = true },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_LOGI(TAG, "display + LVGL ready");
    return disp;
}
