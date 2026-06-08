/*
 * SPDX-License-Identifier: MIT
 *
 * Kitchen-sink demo. Renders five feature screens — neopixel, button, touch,
 * wifi, psram — on the ST7701 panel. Navigate by swiping left/right on the
 * touchscreen or pressing USER_SW; there is no auto-advance.
 *
 * Each screen fully repaints in render(); because the driver is double-buffered
 * the back buffer is two frames stale after a flip, so a change repaints for two
 * consecutive presents (redraw_frames). Animated screens report a change every
 * tick and so repaint continuously.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "gfx.h"
#include "screens.h"
#include "touch.h"

LOG_MODULE_REGISTER(kitchen_sink, LOG_LEVEL_INF);

#define TICK_MS       50
#define REDRAW_FRAMES 2

static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static const struct screen *(*const factories[])(void) = {
	screen_neopixel,
	screen_button,
	screen_touch,
	screen_wifi,
	screen_psram,
};

static const struct screen *screens[ARRAY_SIZE(factories)];
static size_t current;
static int last_button;
static int redraw_frames;

static void show(size_t idx)
{
	if (screens[current] != NULL && screens[current]->leave != NULL) {
		screens[current]->leave();
	}

	current = idx;

	const struct screen *s = screens[current];

	LOG_INF("--- screen: %s ---", s->name);
	if (s->enter != NULL) {
		(void)s->enter();
	}
	redraw_frames = REDRAW_FRAMES;
}

static size_t step_idx(int delta)
{
	int n = ARRAY_SIZE(screens);

	return (size_t)(((int)current + delta + n) % n);
}

int main(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(factories); i++) {
		screens[i] = factories[i]();
	}

	(void)gfx_init();
	if (!gfx_ready()) {
		LOG_WRN("no display — navigation/logic still runs headless");
	}

	if (device_is_ready(sw.port)) {
		(void)gpio_pin_configure_dt(&sw, GPIO_INPUT);
	} else {
		LOG_WRN("USER_SW not ready");
	}

	LOG_INF("kitchen_sink: %u screens — swipe or USER_SW to navigate",
		(unsigned)ARRAY_SIZE(screens));

	last_button = -1;
	show(0);

	while (1) {
		if (screens[current]->update != NULL && screens[current]->update()) {
			redraw_frames = REDRAW_FRAMES;
		}

		/* Navigation: touch swipe, then USER_SW rising edge. */
		switch (touch_nav_take()) {
		case NAV_LEFT:
			show(step_idx(+1));
			break;
		case NAV_RIGHT:
			show(step_idx(-1));
			break;
		default:
			break;
		}

		int v = gpio_pin_get_dt(&sw);

		if (v == 1 && last_button == 0) {
			show(step_idx(+1));
		}
		if (v >= 0) {
			last_button = v;
		}

		if (redraw_frames > 0 && gfx_ready()) {
			if (screens[current]->render != NULL) {
				screens[current]->render();
			}
			gfx_present();
			redraw_frames--;
		}

		k_msleep(TICK_MS);
	}

	return 0;
}
