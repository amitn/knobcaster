// DRV2605 haptic driver: a short "click" per encoder detent. The I2C writes run
// on a dedicated worker task so callers (e.g. the encoder poll task) never block.
#include "haptics.h"
#include "board_pins.h"
#include "board_i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "haptics";

// DRV2605 registers.
#define DRV_REG_MODE      0x01
#define DRV_REG_LIBRARY   0x03
#define DRV_REG_WAVESEQ1  0x04
#define DRV_REG_WAVESEQ2  0x05
#define DRV_REG_GO        0x0C
#define DRV_REG_FEEDBACK  0x1A

#define DRV_MODE_INTTRIG  0x00   // internal trigger, out of standby
#define DRV_LIBRARY_ERM_A 0x01   // ERM waveform library A
#define DRV_EFFECT_CLICK  0x01   // waveform 1 = "Strong Click - 100%"

static i2c_master_dev_handle_t s_dev;
static QueueHandle_t           s_q;

static esp_err_t drv_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

// Worker: each queued token triggers one waveform playback (~tens of ms).
static void haptics_task(void *arg)
{
    uint8_t tok;
    for (;;) {
        if (xQueueReceive(s_q, &tok, portMAX_DELAY) != pdTRUE) continue;
        drv_write(DRV_REG_WAVESEQ1, DRV_EFFECT_CLICK);
        drv_write(DRV_REG_WAVESEQ2, 0x00);   // end of sequence
        drv_write(DRV_REG_GO, 0x01);         // play
    }
}

void haptics_start(void)
{
    if (s_dev) return;

    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (!bus) { ESP_LOGW(TAG, "no i2c bus; haptics disabled"); return; }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BOARD_DRV2605_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "DRV2605 add_device failed; haptics disabled");
        s_dev = NULL;
        return;
    }

    // Out of standby into internal-trigger mode, select an ERM click library.
    if (drv_write(DRV_REG_MODE, DRV_MODE_INTTRIG) != ESP_OK) {
        ESP_LOGW(TAG, "DRV2605 not responding; haptics disabled");
        s_dev = NULL;            // chip absent / wrong actuator wiring
        return;
    }
    drv_write(DRV_REG_LIBRARY, DRV_LIBRARY_ERM_A);

    // Short queue: drop clicks during very fast spins rather than lag behind.
    s_q = xQueueCreate(2, sizeof(uint8_t));
    xTaskCreate(haptics_task, "haptics", 2560, NULL, 4, NULL);
    ESP_LOGI(TAG, "DRV2605 ready (I2C 0x%02X)", BOARD_DRV2605_ADDR);
}

void haptics_click(void)
{
    if (!s_q) return;
    uint8_t tok = 1;
    xQueueSend(s_q, &tok, 0);   // non-blocking; drop if a click is mid-flight
}
