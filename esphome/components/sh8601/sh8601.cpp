#include "sh8601.h"

#include "esphome/core/log.h"

#include "driver/spi_master.h"
#include "esp_lcd_sh8601.h"
#include "sh8601_init_cmds.h"  // shared init table (components/bsp/include, single source)

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
  ESP_LOGCONFIG(TAG, "SH8601 %dx%d ready", width_, height_);
}

void SH8601::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                            display::ColorOrder order, display::ColorBitness bitness,
                            bool big_endian, int x_offset, int y_offset, int x_pad) {
  if (panel_ == nullptr) return;
  // esp_lcd uses exclusive end coordinates and a tightly-packed RGB565 buffer,
  // which is how LVGL flushes a rectangular dirty area (x_offset/x_pad are 0).
  esp_lcd_panel_draw_bitmap(panel_, x_start, y_start, x_start + w, y_start + h, ptr);
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
