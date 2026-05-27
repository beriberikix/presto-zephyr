/*
 * SPDX-License-Identifier: MIT
 *
 * Touch screen: subscribes to INPUT events from the FT6236 and reports the
 * latest touched coordinate when this screen is active.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "screens.h"

LOG_MODULE_REGISTER(screen_touch, LOG_LEVEL_INF);

#if !IS_ENABLED(CONFIG_INPUT_FT6146)

static int touch_enter(void)
{
	LOG_WRN("FT6236 driver disabled in this build");
	return -ENODEV;
}

static void touch_update(void)
{
}

static void touch_leave(void)
{
}

#else

#include <zephyr/device.h>
#include <zephyr/input/input.h>

#define TOUCH_NODE DT_NODELABEL(ft6236)

static const struct device *const touch = DEVICE_DT_GET(TOUCH_NODE);

static struct {
	int16_t x;
	int16_t y;
	bool pressed;
	bool active;
	bool dirty;
} state;

static void on_input(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->dev != touch || !state.active) {
		return;
	}

	switch (evt->code) {
	case INPUT_ABS_X:
		state.x = (int16_t)evt->value;
		state.dirty = true;
		break;
	case INPUT_ABS_Y:
		state.y = (int16_t)evt->value;
		state.dirty = true;
		break;
	case INPUT_BTN_TOUCH:
		state.pressed = evt->value != 0;
		state.dirty = true;
		break;
	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(NULL, on_input, NULL);

static int touch_enter(void)
{
	state.active = true;
	state.dirty = false;

	if (!device_is_ready(touch)) {
		LOG_WRN("Touch controller not ready");
		return -ENODEV;
	}

	LOG_INF("Touch screen ready");
	return 0;
}

static void touch_update(void)
{
	if (!state.dirty) {
		return;
	}

	LOG_INF("touch: x=%d y=%d %s", state.x, state.y,
		state.pressed ? "down" : "up");
	state.dirty = false;
}

static void touch_leave(void)
{
	state.active = false;
}

#endif /* CONFIG_INPUT_FT6146 */

static const struct screen instance = {
	.name = "touch",
	.enter = touch_enter,
	.update = touch_update,
	.leave = touch_leave,
};

const struct screen *screen_touch(void)
{
	return &instance;
}
