#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display/display.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

namespace esphome {
namespace sh8601 {

// SH8601 QSPI AMOLED as an ESPHome display::Display, backed by the
// espressif/esp_lcd_sh8601 IDF driver (which frames QSPI commands correctly —
// unlike ESPHome's built-in qspi_dbi). LVGL flushes via draw_pixels_at().
class SH8601 : public display::Display {
 public:
  void setup() override;
  void loop() override;      // polls the serial console for the 'S' screenshot key
  void update() override {}  // LVGL drives draws; nothing periodic
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Stream the current framebuffer over USB-Serial/JTAG (same format as the
  // vanilla fbdump: `--FBDUMP W H RGB565--` + raw RGB565). Decode with
  // scripts/esphome_shot.py. Also callable from automations.
  void dump_screen();

  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                      display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                      int x_offset, int y_offset, int x_pad) override;
  void draw_pixel_at(int x, int y, Color color) override;  // graphics layer (LVGL uses draw_pixels_at)

  int get_width_internal() override { return width_; }
  int get_height_internal() override { return height_; }
  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }

  void set_pins(int cs, int rst, int clk, int d0, int d1, int d2, int d3) {
    cs_ = cs; rst_ = rst; clk_ = clk;
    d0_ = d0; d1_ = d1; d2_ = d2; d3_ = d3;
  }
  void set_dimensions(int w, int h) { width_ = w; height_ = h; }

 protected:
  int cs_{0}, rst_{0}, clk_{0}, d0_{0}, d1_{0}, d2_{0}, d3_{0};
  int width_{360}, height_{360};
  esp_lcd_panel_handle_t panel_{nullptr};
  uint16_t *fb_{nullptr};       // PSRAM mirror of what's on screen, for screenshots
  uint8_t *dma_buf_{nullptr};   // persistent internal-DMA bounce for the SPI flush
  size_t dma_cap_{0};
};

}  // namespace sh8601
}  // namespace esphome
