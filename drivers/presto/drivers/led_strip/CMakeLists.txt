# Copyright (c) 2026 Jonathan Beri
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_WS2812_STRIP_PRESTO_PIO ws2812_presto_pio.c)
