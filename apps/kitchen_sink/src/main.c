/*
 * SPDX-License-Identifier: MIT
 *
 * Kitchen-sink demo. Cycles through four "screens" — neopixel, button, touch,
 * wifi — running each one's update() on a 100 ms tick. Press USER_SW to step
 * to the next screen; otherwise the demo advances every 5 seconds.
 *
 * The Presto's display is not yet supported in this port (see top-level
 * README), so screens render to the log instead of to a panel. The screen
 * dispatcher is the same shape the LVGL version would have.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "screens.h"

LOG_MODULE_REGISTER(kitchen_sink, LOG_LEVEL_INF);

#define TICK_MS		100
#define AUTO_ADVANCE_MS	5000

static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static const struct screen *(*const factories[])(void) = {
	screen_neopixel,
	screen_button,
	screen_touch,
	screen_wifi,
};

static const struct screen *screens[ARRAY_SIZE(factories)];
static size_t current;
static int last_button;
static int64_t last_change_ms;

static void show(size_t idx)
{
	if (screens[current] != NULL && screens[current]->leave != NULL) {
		screens[current]->leave();
	}

	current = idx;
	last_change_ms = k_uptime_get();

	const struct screen *s = screens[current];

	LOG_INF("--- screen: %s ---", s->name);

	if (s->enter != NULL) {
		(void)s->enter();
	}
}

int main(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(factories); i++) {
		screens[i] = factories[i]();
	}

	if (device_is_ready(sw.port)) {
		(void)gpio_pin_configure_dt(&sw, GPIO_INPUT);
	} else {
		LOG_WRN("USER_SW not ready — auto-cycling only");
	}

	LOG_INF("kitchen_sink: %u screens, tick=%dms, auto-advance=%dms",
		(unsigned)ARRAY_SIZE(screens), TICK_MS, AUTO_ADVANCE_MS);

	last_button = -1;
	last_change_ms = k_uptime_get();
	show(0);

	while (1) {
		if (screens[current]->update != NULL) {
			screens[current]->update();
		}

		int v = gpio_pin_get_dt(&sw);

		if (v == 1 && last_button == 0) {
			show((current + 1) % ARRAY_SIZE(screens));
		}

		if (v >= 0) {
			last_button = v;
		}

		if (k_uptime_get() - last_change_ms > AUTO_ADVANCE_MS) {
			show((current + 1) % ARRAY_SIZE(screens));
		}

		k_msleep(TICK_MS);
	}

	return 0;
}
