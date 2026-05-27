/*
 * SPDX-License-Identifier: MIT
 *
 * Button screen: snapshots USER_SW level on each tick.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "screens.h"

LOG_MODULE_REGISTER(screen_button, LOG_LEVEL_INF);

static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static int last_logged = -1;

static int button_enter(void)
{
	if (!device_is_ready(sw.port)) {
		LOG_WRN("USER_SW port not ready");
		return -ENODEV;
	}

	(void)gpio_pin_configure_dt(&sw, GPIO_INPUT);
	last_logged = -1;
	LOG_INF("Button screen ready");
	return 0;
}

static void button_update(void)
{
	int v = gpio_pin_get_dt(&sw);

	if (v >= 0 && v != last_logged) {
		LOG_INF("USER_SW = %s", v ? "PRESSED" : "released");
		last_logged = v;
	}
}

static void button_leave(void)
{
}

static const struct screen instance = {
	.name = "button",
	.enter = button_enter,
	.update = button_update,
	.leave = button_leave,
};

const struct screen *screen_button(void)
{
	return &instance;
}
