/*
 * SPDX-License-Identifier: MIT
 *
 * Shared FT6236 touch handling for kitchen_sink: a single INPUT subscriber that
 * feeds both a live touch point (for the Touch screen to draw) and a left/right
 * swipe detector (for navigation). The swipe request is produced in the input
 * callback thread and consumed by the main loop.
 */

#ifndef KITCHEN_SINK_TOUCH_H_
#define KITCHEN_SINK_TOUCH_H_

#include <stdbool.h>
#include <stdint.h>

/* The FT6236 reports in the panel's native pixel space (~0..479), not the raw
 * 12-bit ADC range, so this is also the swipe/scale reference.
 */
#define TOUCH_RAW_MAX 480

/* Latest touch sample, in the controller's coordinate space (0..TOUCH_RAW_MAX). */
struct touch_point {
	int16_t x;
	int16_t y;
	bool pressed;
};

enum nav_dir {
	NAV_NONE = 0,
	NAV_LEFT,  /* swipe right-to-left -> next screen */
	NAV_RIGHT, /* swipe left-to-right -> previous screen */
};

/* Snapshot the latest touch point. */
void touch_get(struct touch_point *out);

/* Atomically read-and-clear a pending swipe navigation request. */
enum nav_dir touch_nav_take(void);

#endif /* KITCHEN_SINK_TOUCH_H_ */
