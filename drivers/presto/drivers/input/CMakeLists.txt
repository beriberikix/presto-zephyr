# Copyright (c) 2026 Jonathan Beri
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_INPUT_PIMORONI_SCALER input_scaler.c)
