/*
 * SPDX-License-Identifier: MIT
 *
 * Touch screen: draws a dot at the current FT6236 touch point and shows the
 * raw coordinates. The shared touch subscriber lives in touch.c.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "screens.h"
#include "touch.h"
#include "ui.h"

LOG_MODULE_REGISTER(screen_touch, LOG_LEVEL_INF);

static struct touch_point last;

static int touch_enter(void)
{
	last.x = 0;
	last.y = 0;
	last.pressed = false;
#if !IS_ENABLED(CONFIG_INPUT_FT6146)
	LOG_WRN("FT6236 driver disabled in this build");
#endif
	return 0;
}

static bool touch_update(void)
{
	struct touch_point t;

	touch_get(&t);
	if (t.x == last.x && t.y == last.y && t.pressed == last.pressed) {
		return false;
	}
	last = t;
	return true;
}

static void touch_render(void)
{
	ui_begin("Touch", COL_GREEN);

#if IS_ENABLED(CONFIG_INPUT_FT6146)
	if (last.pressed) {
		int px = (int)last.x * gfx_width() / TOUCH_RAW_MAX;
		int py = (int)last.y * gfx_height() / TOUCH_RAW_MAX;

		gfx_fill_rect(px - 6, py - 6, 12, 12, COL_WHITE);
	}

	gfx_text(8, 44, last.pressed ? "touching" : "touch the screen", COL_GREY,
		 UI_BG, 1);

	char buf[24];

	snprintf(buf, sizeof(buf), "x=%4d y=%4d", last.x, last.y);
	gfx_text(8, gfx_height() - 30, buf, COL_WHITE, UI_BG, 1);
#else
	gfx_text(8, 96, "touch n/a", COL_GREY, UI_BG, 2);
#endif

	ui_footer();
}

static void touch_leave(void)
{
}

static const struct screen instance = {
	.name = "touch",
	.enter = touch_enter,
	.update = touch_update,
	.render = touch_render,
	.leave = touch_leave,
};

const struct screen *screen_touch(void)
{
	return &instance;
}
