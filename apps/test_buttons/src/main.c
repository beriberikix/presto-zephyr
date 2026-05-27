/*
 * SPDX-License-Identifier: MIT
 *
 * Polls USER_SW (the BOOTSEL-shared button) and logs press/release edges.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_buttons, LOG_LEVEL_INF);

static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

int main(void)
{
	if (!device_is_ready(sw.port)) {
		LOG_ERR("USER_SW port not ready");
		return -ENODEV;
	}

	if (gpio_pin_configure_dt(&sw, GPIO_INPUT) != 0) {
		LOG_ERR("USER_SW configure failed");
		return -EIO;
	}

	LOG_INF("USER_SW polling started (sw0 = GP%d)", sw.pin);

	int last = -1;

	while (1) {
		int v = gpio_pin_get_dt(&sw);

		if (v >= 0 && v != last) {
			last = v;
			LOG_INF("USER_SW: %s", v ? "pressed" : "released");
		}

		k_msleep(20);
	}

	return 0;
}
