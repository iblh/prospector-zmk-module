/* SPDX-License-Identifier: MIT */

#pragma once

#include <stdbool.h>
#include <zephyr/types.h>

int prospector_brightness_auto_on(void);
int prospector_brightness_manual_on(void);
int prospector_brightness_toggle_auto(void);
int prospector_brightness_manual_step(int direction);
int prospector_brightness_set_manual(uint8_t brightness);
uint8_t prospector_brightness_get_manual(void);
bool prospector_brightness_is_auto(void);
