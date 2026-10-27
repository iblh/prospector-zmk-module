/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

/* All functions must run on the LVGL display work queue. */
void prospector_device_screen_open(void);
bool prospector_device_screen_is_open(void);
void prospector_device_screen_on_idle(void);
