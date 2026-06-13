/*
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 *
 * LVGL proof-of-concept: a themed label + button + slider, driven by touch.
 * On the Presto the LVGL object heap lives in PSRAM (see the board .conf) and
 * the FT6236's panel-space touch coordinates are halved onto the 240x240
 * half-res display by the pimoroni,input-scaler node (see the board overlay).
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_REGISTER(test_lvgl, LOG_LEVEL_INF);

static uint32_t count;
static lv_obj_t *count_label;

static void btn_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	count++;
	lv_label_set_text_fmt(count_label, "Count: %u", count);
}

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display)) {
		LOG_ERR("display not ready");
		return 0;
	}

	/*
	 * Fixed layout: stop the screen from scrolling. Otherwise a press-drag
	 * (e.g. on the slider) is also taken as a scroll gesture and the whole
	 * screen elastically scrolls and bounces back.
	 */
	lv_obj_t *scr = lv_screen_active();

	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

	lv_obj_t *title = lv_label_create(scr);

	lv_label_set_text(title, "Presto LVGL PoC");
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

	lv_obj_t *btn = lv_button_create(scr);

	lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);
	lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *btn_label = lv_label_create(btn);

	lv_label_set_text(btn_label, "Tap me");

	count_label = lv_label_create(scr);
	lv_label_set_text(count_label, "Count: 0");
	lv_obj_align(count_label, LV_ALIGN_CENTER, 0, 30);

	lv_obj_t *slider = lv_slider_create(scr);

	lv_obj_set_width(slider, lv_pct(70));
	lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -24);

	lv_timer_handler();
	display_blanking_off(display);

	LOG_INF("LVGL UI up (%ux%u)", lv_display_get_horizontal_resolution(NULL),
		lv_display_get_vertical_resolution(NULL));

	while (1) {
		uint32_t sleep_ms = lv_timer_handler();

		k_msleep(MIN(sleep_ms, 100));
	}
	return 0;
}
