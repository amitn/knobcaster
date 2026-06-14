#include "fbdump.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#define FB_W 360
#define FB_H 360

// Raw write over USB-Serial/JTAG (no VFS newline translation — binary-safe).
static void usj_write(const void *data, size_t len)
{
    const uint8_t *p = data;
    size_t off = 0;
    while (off < len) {
        int w = usb_serial_jtag_write_bytes(p + off, len - off, pdMS_TO_TICKS(1000));
        if (w <= 0) break;
        off += (size_t)w;
    }
}

static void dump_now(void)
{
    lvgl_port_lock(0);
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    lvgl_port_unlock();
    if (!snap) { usj_write("\n--FBERR--\n", 11); return; }

    // Silence logs so nothing interleaves into the binary stream.
    esp_log_level_set("*", ESP_LOG_NONE);
    char hdr[48];
    int hn = snprintf(hdr, sizeof(hdr), "\n--FBDUMP %d %d RGB565--\n", FB_W, FB_H);
    usj_write(hdr, hn);
    usj_write(snap->data, (size_t)FB_W * FB_H * 2);
    usj_write("\n--FBEND--\n", 11);
    esp_log_level_set("*", ESP_LOG_INFO);

    lvgl_port_lock(0);
    lv_draw_buf_destroy(snap);
    lvgl_port_unlock();
}

static void fbdump_task(void *arg)
{
    uint8_t c;
    for (;;) {
        int n = usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(200));
        if (n == 1 && (c == 'S' || c == 's')) dump_now();
    }
}

void fbdump_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    if (usb_serial_jtag_driver_install(&cfg) == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();   // route console I/O through the driver too
    }
    xTaskCreate(fbdump_task, "fbdump", 6144, NULL, 3, NULL);
}
