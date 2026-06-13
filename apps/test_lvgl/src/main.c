/*
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 *
 * LVGL proof-of-concept: a themed label + button + slider, driven by touch.
 * On the Presto the LVGL object heap lives in PSRAM (see the board .conf)
 * and the FT6236's raw 0-480 coordinates are halved onto the 240x240
 * half-res display by wrapping the indev read callback.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <lvgl_input_device.h>

LOG_MODULE_REGISTER(test_lvgl, LOG_LEVEL_INF);

static uint32_t count;
static lv_obj_t *count_label;

static void btn_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	count++;
	lv_label_set_text_fmt(count_label, "Count: %u", count);
}

#ifdef CONFIG_ST7701_PRESTO_HALF_RES
/*
 * The touch controller reports panel-space 0-480 coordinates; the LVGL
 * display is the 240x240 half-res framebuffer. Neither the LVGL pointer
 * glue nor the input subsystem can scale, so chain the glue's read
 * callback and halve the point it produced.
 */
static lv_indev_read_cb_t orig_read_cb;

static void scaled_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	orig_read_cb(indev, data);
	data->point.x /= 2;
	data->point.y /= 2;
}

static void install_touch_scaler(void)
{
	lv_indev_t *indev = lvgl_input_get_indev(
		DEVICE_DT_GET(DT_NODELABEL(lvgl_pointer)));

	if (indev == NULL) {
		LOG_ERR("LVGL pointer indev not found");
		return;
	}
	orig_read_cb = lv_indev_get_read_cb(indev);
	lv_indev_set_read_cb(indev, scaled_read_cb);
}
#else
static void install_touch_scaler(void)
{
}
#endif

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display)) {
		LOG_ERR("display not ready");
		return 0;
	}

	install_touch_scaler();

	lv_obj_t *title = lv_label_create(lv_screen_active());

	lv_label_set_text(title, "Presto LVGL PoC");
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

	lv_obj_t *btn = lv_button_create(lv_screen_active());

	lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);
	lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *btn_label = lv_label_create(btn);

	lv_label_set_text(btn_label, "Tap me");

	count_label = lv_label_create(lv_screen_active());
	lv_label_set_text(count_label, "Count: 0");
	lv_obj_align(count_label, LV_ALIGN_CENTER, 0, 30);

	lv_obj_t *slider = lv_slider_create(lv_screen_active());

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
