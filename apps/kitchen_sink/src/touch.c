/*
 * SPDX-License-Identifier: MIT
 *
 * FT6236 touch + swipe detector (see touch.h). One global INPUT callback owns
 * the touch device for the whole app; screen_touch.c reads the live point via
 * touch_get(). On native_sim (no FT6236) this compiles to stubs.
 */

#include <zephyr/kernel.h>

#include "touch.h"

#if IS_ENABLED(CONFIG_INPUT_FT6146)

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <stdlib.h>

#define TOUCH_NODE      DT_NODELABEL(ft6236)
#define SWIPE_DX_THRESH (TOUCH_RAW_MAX / 4) /* ~1/4 of the screen */

static const struct device *const touch = DEVICE_DT_GET(TOUCH_NODE);

/*
 * Live point packed into one atomic word so touch_get() can read it lock-free
 * while the input-callback thread updates it: x[11:0] y[23:12] pressed[24].
 */
static atomic_t live;

#define LIVE_PACK(x, y, p)                                                                          \
	(((uint32_t)((x) & 0xFFF)) | ((uint32_t)((y) & 0xFFF) << 12) | ((uint32_t)((p) ? 1 : 0) << 24))

static atomic_t pending_nav = ATOMIC_INIT(NAV_NONE);

/* Swipe FSM + current sample — only ever touched from the (serialised) input
 * callback thread.
 */
static int16_t cx, cy;
static bool cp;
static int16_t down_x, down_y;
static bool tracking;

static void on_input(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->dev != touch) {
		return;
	}

	switch (evt->code) {
	case INPUT_ABS_X:
		cx = (int16_t)evt->value;
		break;
	case INPUT_ABS_Y:
		cy = (int16_t)evt->value;
		break;
	case INPUT_BTN_TOUCH: {
		bool now = evt->value != 0;

		/* The FT6236 re-reports BTN_TOUCH on every sample, so latch the swipe
		 * origin only on the press edge and evaluate on the release edge.
		 */
		if (now && !cp) {
			down_x = cx;
			down_y = cy;
			tracking = true;
		} else if (!now && cp && tracking) {
			int dx = cx - down_x;
			int dy = cy - down_y;

			if (abs(dx) > SWIPE_DX_THRESH && abs(dx) > abs(dy)) {
				atomic_set(&pending_nav, dx < 0 ? NAV_LEFT : NAV_RIGHT);
			}
			tracking = false;
		}
		cp = now;
		break;
	}
	default:
		break;
	}

	atomic_set(&live, LIVE_PACK(cx, cy, cp));
}

INPUT_CALLBACK_DEFINE(NULL, on_input, NULL);

void touch_get(struct touch_point *out)
{
	uint32_t v = (uint32_t)atomic_get(&live);

	out->x = v & 0xFFF;
	out->y = (v >> 12) & 0xFFF;
	out->pressed = (v >> 24) & 1;
}

enum nav_dir touch_nav_take(void)
{
	return (enum nav_dir)atomic_set(&pending_nav, NAV_NONE);
}

#else /* no touch controller */

void touch_get(struct touch_point *out)
{
	out->x = 0;
	out->y = 0;
	out->pressed = false;
}

enum nav_dir touch_nav_take(void)
{
	return NAV_NONE;
}

#endif /* CONFIG_INPUT_FT6146 */
