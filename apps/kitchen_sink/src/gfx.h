/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal RGB565 drawing layer over the Zephyr display API for the kitchen_sink
 * demo: solid rectangles and 8x8 bitmap-font text, presented tear-free via the
 * ST7701 vblank flip when double-buffering is enabled. Coordinates are in the
 * display's pixel space (240x240 on the Presto at half-res). All colours are
 * standard little-endian PIXEL_FORMAT_RGB_565; display_write() byteswaps them
 * for the panel.
 */

#ifndef KITCHEN_SINK_GFX_H_
#define KITCHEN_SINK_GFX_H_

#include <stdbool.h>
#include <stdint.h>

/* RGB565 palette (little-endian, as accepted by display_write). */
#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_RED    0xF800
#define COL_GREEN  0x07E0
#define COL_BLUE   0x001F
#define COL_YELLOW 0xFFE0
#define COL_CYAN   0x07FF
#define COL_MAGENTA 0xF81F
#define COL_AMBER  0xFD20
#define COL_GREY   0x8410
#define COL_DKGREY 0x4208
#define COL_NAVY   0x0010

/* Resolve the chosen display, select RGB565, cache the (clamped) size and turn
 * blanking off. Returns 0 on success, negative if no usable display.
 */
int gfx_init(void);
bool gfx_ready(void);
uint16_t gfx_width(void);
uint16_t gfx_height(void);

/* Fill the whole screen / a clipped rectangle with a solid colour. */
void gfx_clear(uint16_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);

/* Draw a NUL-terminated ASCII string with the 8x8 font at integer scale
 * (1 -> 8 px tall, 2 -> 16 px, ...). Both fg and bg are opaque; draw onto a
 * known background (pass that colour as bg). gfx_text_w() returns the pixel
 * width of a string for centring.
 */
void gfx_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
int gfx_text_w(const char *s, int scale);

/* Present the rendered frame (vblank flip if double-buffered; else a no-op). */
void gfx_present(void);

#endif /* KITCHEN_SINK_GFX_H_ */
