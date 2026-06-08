# Copyright (c) 2026 Jonathan Beri
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PIMORONI_RP2350_PSRAM psram_aps6404.c)
