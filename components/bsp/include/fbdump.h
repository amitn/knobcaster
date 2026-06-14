// Debug: dump the LCD framebuffer over the serial console as raw RGB565.
//
// Spawns a task that watches stdin; when it receives 'S' it renders the active
// screen with lv_snapshot and writes:
//   "\n--FBDUMP 360 360 RGB565--\n" <W*H*2 raw little-endian RGB565 bytes> "\n--FBEND--\n"
// Logs are silenced during the transfer so the binary isn't corrupted. Pair with
// `just shot` (scripts/fbdump.py) to save a PNG.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void fbdump_start(void);

#ifdef __cplusplus
}
#endif
