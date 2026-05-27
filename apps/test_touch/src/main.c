/*
 * SPDX-License-Identifier: MIT
 *
 * Subscribes to INPUT events from the FT6236 capacitive touch controller
 * and logs (x, y, pressed) tuples.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_touch, LOG_LEVEL_INF);

#if !IS_ENABLED(CONFIG_INPUT_FT6146)

int main(void)
{
	LOG_INF("FT6236 driver not enabled in this build (CONFIG_INPUT_FT6146=n)");

	while (1) {
		k_msleep(1000);
	}

	return 0;
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
	bool dirty;
} state;

static void on_input(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->dev != touch) {
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

	if (evt->sync && state.dirty) {
		LOG_INF("touch: x=%d y=%d %s",
			state.x, state.y, state.pressed ? "down" : "up");
		state.dirty = false;
	}
}

INPUT_CALLBACK_DEFINE(NULL, on_input, NULL);

int main(void)
{
	if (!device_is_ready(touch)) {
		LOG_ERR("Touch controller %s not ready", touch->name);
		return -ENODEV;
	}

	LOG_INF("Touch test ready — tap the screen");

	while (1) {
		k_msleep(1000);
	}

	return 0;
}

#endif /* CONFIG_INPUT_FT6146 */
