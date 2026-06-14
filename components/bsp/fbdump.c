#include "fbdump.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#define FB_W 360
#define FB_H 360

static void dump_now(void)
{
    lvgl_port_lock(0);
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    lvgl_port_unlock();
    if (!snap) { printf("\n--FBERR--\n"); fflush(stdout); return; }

    // Silence logs so other tasks don't interleave bytes into the binary stream.
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("\n--FBDUMP %d %d RGB565--\n", FB_W, FB_H);
    fflush(stdout);
    fwrite(snap->data, 1, (size_t)FB_W * FB_H * 2, stdout);
    fflush(stdout);
    printf("\n--FBEND--\n");
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);

    lvgl_port_lock(0);
    lv_draw_buf_destroy(snap);
    lvgl_port_unlock();
}

static void fbdump_task(void *arg)
{
    for (;;) {
        int c = getchar();
        if (c == 'S' || c == 's') dump_now();
        else if (c == EOF)        vTaskDelay(pdMS_TO_TICKS(50));
        else                      vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void fbdump_start(void)
{
    xTaskCreate(fbdump_task, "fbdump", 6144, NULL, 3, NULL);
}
