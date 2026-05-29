# Copyright (c) 2026 Jonathan Beri
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ST7701_PRESTO st7701_presto.c)
