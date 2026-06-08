/*
 * SPDX-License-Identifier: MIT
 *
 * Screen interface for the kitchen_sink demo. Each "screen" is a name plus
 * callbacks. The main loop runs update() every tick (returning true when the
 * content changed) and calls render() to repaint the whole screen when a
 * present is due.
 */

#ifndef SCREENS_H_
#define SCREENS_H_

#include <stdbool.h>

struct screen {
	const char *name;
	int  (*enter)(void);    /* activate; return <0 if the feature is absent */
	bool (*update)(void);   /* advance state; return true if it changed */
	void (*render)(void);   /* paint the ENTIRE screen to the back buffer */
	void (*leave)(void);
};

const struct screen *screen_neopixel(void);
const struct screen *screen_button(void);
const struct screen *screen_touch(void);
const struct screen *screen_wifi(void);
const struct screen *screen_psram(void);

#endif /* SCREENS_H_ */
