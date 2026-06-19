#include "sh8601.h"

#include "esphome/core/log.h"

#include "esphome/components/logger/logger.h"

#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_lcd_sh8601.h"
#include "sh8601_init_cmds.h"  // shared init table (components/bsp/include, single source)

#include <cstring>

namespace esphome {
namespace sh8601 {

static const char *const TAG = "sh8601";

void SH8601::setup() {
  // QSPI bus + SH8601 panel — mirrors components/bsp/display.c::display_init().
  spi_bus_config_t buscfg = {};
  buscfg.data0_io_num = d0_;
  buscfg.data1_io_num = d1_;
  buscfg.data2_io_num = d2_;
  buscfg.data3_io_num = d3_;
  buscfg.sclk_io_num = clk_;
  buscfg.max_transfer_sz = width_ * height_ * 2;
  if (spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize failed");
    this->mark_failed();
    return;
  }

  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(cs_, nullptr, nullptr);
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI2_HOST, &io_cfg, &io) != ESP_OK) {
    ESP_LOGE(TAG, "panel_io_spi failed");
    this->mark_failed();
    return;
  }

  sh8601_vendor_config_t vendor = {};
  vendor.init_cmds = sh8601_init_cmds;
  vendor.init_cmds_size = sizeof(sh8601_init_cmds) / sizeof(sh8601_init_cmds[0]);
  vendor.flags.use_qspi_interface = 1;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = rst_;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config = &vendor;
  if (esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel_) != ESP_OK) {
    ESP_LOGE(TAG, "new_panel_sh8601 failed");
    this->mark_failed();
    return;
  }

  esp_lcd_panel_reset(panel_);
  esp_lcd_panel_init(panel_);
  esp_lcd_panel_disp_on_off(panel_, true);

  // Clear power-on garbage to black, row by row from a small internal-DMA buffer
  // (so undrawn-by-LVGL areas aren't left as artifacts).
  uint16_t *blank = (uint16_t *) heap_caps_calloc(width_, 2, MALLOC_CAP_DMA);
  if (blank) {
    for (int y = 0; y < height_; y++)
      esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, blank);
    heap_caps_free(blank);
  }

  // Persistent internal-DMA flush buffer, pre-sized so runtime flushes never
  // realloc or fall back to the (unreliable) PSRAM path. 48KB covers ~1/5 screen,
  // comfortably above the LVGL partial-buffer flush size (buffer_size: 12%).
  dma_cap_ = 48 * 1024;
  dma_buf_ = (uint8_t *) heap_caps_malloc(dma_cap_, MALLOC_CAP_DMA);
  if (dma_buf_ == nullptr) dma_cap_ = 0;

  // Screenshot mirror (optional — screenshots disabled if it won't allocate).
  fb_ = (uint16_t *) heap_caps_malloc((size_t) width_ * height_ * 2, MALLOC_CAP_SPIRAM);
  ESP_LOGCONFIG(TAG, "SH8601 %dx%d ready (screenshot=%s)", width_, height_, fb_ ? "on" : "off");
}

void SH8601::dump_screen() {
  if (fb_ == nullptr) return;
  // Silence the (shared) console logger during the dump — otherwise its text
  // congests the USB-Serial/JTAG TX and corrupts/stalls the binary stream. This
  // also gates ESP-IDF logs from the cast task. Restore afterwards.
  int prev_level = ESPHOME_LOG_LEVEL_DEBUG;
  if (logger::global_logger != nullptr) {
    prev_level = logger::global_logger->get_log_level();
    logger::global_logger->set_log_level(ESPHOME_LOG_LEVEL_NONE);
  }

  // Header + raw RGB565, decoded by scripts/fbdump.py (`just shot`). The write
  // runs synchronously on the main loop; usb_serial_jtag_write_bytes needs an
  // internal-RAM source, so the PSRAM mirror is bounced in chunks.
  char hdr[40];
  int n = snprintf(hdr, sizeof(hdr), "\n--FBDUMP %d %d RGB565--\n", width_, height_);
  usb_serial_jtag_write_bytes((const uint8_t *) hdr, n, pdMS_TO_TICKS(1000));

  static uint8_t bounce[256];  // internal RAM
  const uint8_t *src = (const uint8_t *) fb_;
  size_t total = (size_t) width_ * height_ * 2, off = 0;
  while (off < total) {
    size_t chunk = total - off;
    if (chunk > sizeof(bounce)) chunk = sizeof(bounce);
    // Byte-swap each pixel: the panel buffer is big-endian RGB565, but
    // scripts/fbdump.py decodes little-endian. (chunk is always even.)
    for (size_t i = 0; i < chunk; i += 2) {
      bounce[i] = src[off + i + 1];
      bounce[i + 1] = src[off + i];
    }
    size_t w = 0;
    while (w < chunk) {
      // Block until the host drains the FIFO (portMAX_DELAY): a fixed timeout
      // bails when the TX backs up, which truncated the stream to 0 bytes.
      int k = usb_serial_jtag_write_bytes(bounce + w, chunk - w, portMAX_DELAY);
      if (k <= 0) break;
      w += (size_t) k;
    }
    off += chunk;
  }
  usb_serial_jtag_wait_tx_done(portMAX_DELAY);
  usb_serial_jtag_write_bytes((const uint8_t *) "\n--FBEND--\n", 11, pdMS_TO_TICKS(1000));

  if (logger::global_logger != nullptr)
    logger::global_logger->set_log_level(prev_level);
}

void SH8601::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                            display::ColorOrder order, display::ColorBitness bitness,
                            bool big_endian, int x_offset, int y_offset, int x_pad) {
  if (panel_ == nullptr) return;
  // LVGL's buffer is in PSRAM; passing it straight to esp_lcd makes the SPI
  // driver allocate a per-transfer DMA bounce buffer, which fails intermittently
  // under memory pressure -> dropped regions show as artifacts. Copy into our own
  // persistent internal-DMA buffer so the source is always DMA-capable.
  size_t len = (size_t) w * h * 2;
  const uint8_t *out = (const uint8_t *) ptr;
  if (len > dma_cap_) {
    uint8_t *nb = (uint8_t *) heap_caps_malloc(len, MALLOC_CAP_DMA);
    if (nb != nullptr) {
      heap_caps_free(dma_buf_);
      dma_buf_ = nb;
      dma_cap_ = len;
    }
  }
  if (dma_buf_ != nullptr && len <= dma_cap_) {
    std::memcpy(dma_buf_, ptr, len);
    out = dma_buf_;
  }
  // esp_lcd uses exclusive end coordinates and a tightly-packed RGB565 buffer,
  // which is how LVGL flushes a rectangular dirty area (x_offset/x_pad are 0).
  esp_err_t e = esp_lcd_panel_draw_bitmap(panel_, x_start, y_start, x_start + w, y_start + h, out);
  if (e != ESP_OK)
    ESP_LOGW(TAG, "draw %dx%d@%d,%d failed: %s", w, h, x_start, y_start, esp_err_to_name(e));

  // Mirror the flush into the screenshot framebuffer (row by row into the rect).
  if (fb_ != nullptr) {
    const uint16_t *row = (const uint16_t *) ptr;
    for (int r = 0; r < h; r++) {
      int dy = y_start + r;
      if (dy < 0 || dy >= height_) continue;
      std::memcpy(&fb_[dy * width_ + x_start], &row[r * w], (size_t) w * 2);
    }
  }
}

void SH8601::draw_pixel_at(int x, int y, Color color) {
  if (panel_ == nullptr) return;
  uint16_t px = ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);
  esp_lcd_panel_draw_bitmap(panel_, x, y, x + 1, y + 1, &px);
}

void SH8601::dump_config() {
  ESP_LOGCONFIG(TAG, "SH8601 QSPI AMOLED %dx%d (cs=%d rst=%d clk=%d) via esp_lcd_sh8601",
                width_, height_, cs_, rst_, clk_);
}

}  // namespace sh8601
}  // namespace esphome
