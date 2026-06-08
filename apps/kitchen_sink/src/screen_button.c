/*
 * SPDX-License-Identifier: MIT
 *
 * Button screen: shows the live USER_SW (GP46) state.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "screens.h"
#include "ui.h"

LOG_MODULE_REGISTER(screen_button, LOG_LEVEL_INF);

static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static int level; /* 0/1, or -1 unknown */
static bool ready;

static int button_enter(void)
{
	ready = device_is_ready(sw.port);
	if (ready) {
		(void)gpio_pin_configure_dt(&sw, GPIO_INPUT);
	} else {
		LOG_WRN("USER_SW not ready");
	}
	level = -1;
	return ready ? 0 : -ENODEV;
}

static bool button_update(void)
{
	int v = ready ? gpio_pin_get_dt(&sw) : 0;

	if (v < 0 || v == level) {
		return false;
	}
	level = v;
	return true;
}

static void button_render(void)
{
	bool pressed = level == 1;
	uint16_t box = pressed ? COL_GREEN : COL_DKGREY;
	const char *s = pressed ? "PRESSED" : "released";

	ui_begin("Button", COL_CYAN);
	gfx_fill_rect(40, 72, gfx_width() - 80, 88, box);
	gfx_text((gfx_width() - gfx_text_w(s, 2)) / 2, 108, s, COL_WHITE, box, 2);
	gfx_text(8, 184, "USER_SW = GP46", COL_WHITE, UI_BG, 1);
	ui_footer();
}

static void button_leave(void)
{
}

static const struct screen instance = {
	.name = "button",
	.enter = button_enter,
	.update = button_update,
	.render = button_render,
	.leave = button_leave,
};

const struct screen *screen_button(void)
{
	return &instance;
}
