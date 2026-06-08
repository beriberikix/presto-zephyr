/*
 * SPDX-License-Identifier: MIT
 *
 * Shared screen chrome for kitchen_sink: a title bar with an accent divider and
 * a centred "swipe" hint footer, so every screen has a consistent frame.
 */

#ifndef KITCHEN_SINK_UI_H_
#define KITCHEN_SINK_UI_H_

#include <stdint.h>

#include "gfx.h"

#define UI_BG       COL_NAVY
#define UI_HEADER_H 30

/* Clear to the background, draw the title (scale 2) and an accent divider. */
void ui_begin(const char *title, uint16_t accent);

/* Centred "< swipe >" hint near the bottom edge. */
void ui_footer(void);

#endif /* KITCHEN_SINK_UI_H_ */
