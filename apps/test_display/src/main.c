/*
 * Display bring-up test for the Pimoroni Presto.
 *
 * Portable across targets: it draws to whatever "zephyr,display" chooses.
 *   - presto/rp2350b/m33 : the ST7701 panel (CONFIG_ST7701_PRESTO).
 *   - native_sim         : the SDL display emulator (a window on the host),
 *                          so the drawing logic can be exercised without
 *                          hardware. See boards/native_sim.conf.
 *
 * Draws eight vertical colour bars and animates a white square across them,
 * using only the generic display_write() API.
 *
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_display, LOG_LEVEL_INF);

/* Sized for the largest panel we target (480 wide). */
#define MAX_WIDTH 480
#define CHUNK_H   16
#define BOX       48

static const uint16_t bars[8] = {
	0xF800, /* red */
	0x07E0, /* green */
	0x001F, /* blue */
	0xFFFF, /* white */
	0xFFE0, /* yellow */
	0x07FF, /* cyan */
	0xF81F, /* magenta */
	0x0000, /* black */
};

static uint16_t chunk_buf[MAX_WIDTH * CHUNK_H];
static uint16_t box_buf[BOX * BOX];

/* Paint the colour bars over the whole display, CHUNK_H rows at a time. */
static void draw_bars(const struct device *disp, uint16_t w, uint16_t h)
{
	const uint16_t bar_w = w / 8;
	struct display_buffer_descriptor desc = {
		.width = w,
		.height = CHUNK_H,
		.pitch = w,
		.buf_size = (size_t)w * CHUNK_H * sizeof(uint16_t),
	};

	for (uint16_t x = 0; x < w; x++) {
		uint16_t c = bars[(bar_w ? (x / bar_w) : 0) & 7];

		for (uint16_t row = 0; row < CHUNK_H; row++) {
			chunk_buf[row * w + x] = c;
		}
	}

	for (uint16_t y = 0; y < h; y += CHUNK_H) {
		uint16_t rows = MIN(CHUNK_H, h - y);

		desc.height = rows;
		desc.buf_size = (size_t)w * rows * sizeof(uint16_t);
		display_write(disp, 0, y, &desc, chunk_buf);
	}
}

int main(void)
{
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities caps;
	uint16_t w, h;
	int x = 0, y, dx = 4;

	if (!device_is_ready(disp)) {
		LOG_ERR("display device not ready");
		return 0;
	}

	/* Both the ST7701 and the SDL emulator support RGB565; make sure that
	 * is the active format (the SDL display may default to something else).
	 */
	(void)display_set_pixel_format(disp, PIXEL_FORMAT_RGB_565);

	display_get_capabilities(disp, &caps);
	w = MIN(caps.x_resolution, MAX_WIDTH);
	h = caps.y_resolution;
	y = (h - BOX) / 2;
	LOG_INF("display %ux%u (using %ux%u), fmt 0x%x",
		caps.x_resolution, caps.y_resolution, w, h, caps.current_pixel_format);

	for (int i = 0; i < BOX * BOX; i++) {
		box_buf[i] = 0xFFFF; /* white square */
	}

	const struct display_buffer_descriptor box_desc = {
		.width = BOX,
		.height = BOX,
		.pitch = BOX,
		.buf_size = sizeof(box_buf),
	};

	display_blanking_off(disp);
	LOG_INF("drawing colour bars + animated square");

	while (1) {
		draw_bars(disp, w, h);
		display_write(disp, x, y, &box_desc, box_buf);

		x += dx;
		if (x <= 0 || x >= w - BOX) {
			dx = -dx;
		}

		k_msleep(33);
	}

	return 0;
}
