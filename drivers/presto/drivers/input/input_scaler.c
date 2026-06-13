/*
 * Input coordinate scaler: subscribes to another input device and re-reports
 * its absolute X/Y events scaled by a rational factor (num/denom), passing all
 * other events through unchanged.
 *
 * The Presto's FT6236 reports touch coordinates in the panel's native space
 * (~0-480). When the display runs half-resolution (240x240), those must be
 * halved to match. Doing it here - on the Zephyr input side, before the LVGL
 * pointer glue - is essential: that glue clamps incoming coordinates to the
 * display resolution, so a 0-480 coordinate would be clipped to the top-left
 * quadrant before any later (e.g. LVGL read-callback) scaling could run.
 *
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT pimoroni_input_scaler

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(input_scaler, CONFIG_INPUT_LOG_LEVEL);

struct input_scaler_config {
	const struct device *input_dev;
	int32_t scale_num;
	int32_t scale_denom;
};

static void input_scaler_cb(struct input_event *evt, void *user_data)
{
	const struct device *dev = user_data;
	const struct input_scaler_config *cfg = dev->config;

	if (evt->type == INPUT_EV_ABS &&
	    (evt->code == INPUT_ABS_X || evt->code == INPUT_ABS_Y)) {
		/* int64 intermediate: avoid overflow for arbitrary int32 inputs. */
		int32_t scaled = (int32_t)((int64_t)evt->value * cfg->scale_num /
					   cfg->scale_denom);

		input_report_abs(dev, evt->code, scaled, evt->sync, K_FOREVER);
	} else {
		/* Pass everything else (notably INPUT_BTN_TOUCH) through as-is. */
		input_report(dev, evt->type, evt->code, evt->value, evt->sync, K_FOREVER);
	}
}

static int input_scaler_init(const struct device *dev)
{
	const struct input_scaler_config *cfg = dev->config;

	if (!device_is_ready(cfg->input_dev)) {
		LOG_ERR("source input device not ready");
		return -ENODEV;
	}

	return 0;
}

#define INPUT_SCALER_DEFINE(inst)                                                                  \
	BUILD_ASSERT(DT_INST_PROP(inst, scale_denom) > 0, "scale-denom must be > 0");              \
                                                                                                   \
	INPUT_CALLBACK_DEFINE_NAMED(DEVICE_DT_GET(DT_INST_PHANDLE(inst, input)),                    \
				    input_scaler_cb, (void *)DEVICE_DT_INST_GET(inst),             \
				    input_scaler_cb_##inst);                                       \
                                                                                                   \
	static const struct input_scaler_config input_scaler_config_##inst = {                     \
		.input_dev = DEVICE_DT_GET(DT_INST_PHANDLE(inst, input)),                          \
		.scale_num = DT_INST_PROP(inst, scale_num),                                        \
		.scale_denom = DT_INST_PROP(inst, scale_denom),                                    \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, input_scaler_init, NULL, NULL, &input_scaler_config_##inst,    \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INPUT_SCALER_DEFINE)
