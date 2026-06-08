/* SPDX-License-Identifier: MIT */

#include "ui.h"

void ui_begin(const char *title, uint16_t accent)
{
	gfx_clear(UI_BG);
	gfx_text(8, 7, title, COL_WHITE, UI_BG, 2);
	gfx_fill_rect(0, UI_HEADER_H, gfx_width(), 2, accent);
}

void ui_footer(void)
{
	static const char hint[] = "< swipe >";

	gfx_text((gfx_width() - gfx_text_w(hint, 1)) / 2, gfx_height() - 12, hint,
		 COL_GREY, UI_BG, 1);
}
