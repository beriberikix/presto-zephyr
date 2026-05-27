/*
 * SPDX-License-Identifier: MIT
 *
 * Screen interface for the kitchen_sink demo. Each "screen" is a name plus
 * three callbacks. The main loop cycles them and calls update() every tick.
 */

#ifndef SCREENS_H_
#define SCREENS_H_

#include <stdbool.h>

struct screen {
	const char *name;
	int (*enter)(void);
	void (*update)(void);
	void (*leave)(void);
};

const struct screen *screen_neopixel(void);
const struct screen *screen_button(void);
const struct screen *screen_touch(void);
const struct screen *screen_wifi(void);

#endif /* SCREENS_H_ */
