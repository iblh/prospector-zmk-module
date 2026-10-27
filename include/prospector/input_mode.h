/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* IDs match the three complete base layers in touch_modes.keymap. */
enum prospector_input_mode { PROSPECTOR_MAC_DV, PROSPECTOR_WIN_DV, PROSPECTOR_GAME };
int prospector_input_mode_get(void);
int prospector_input_mode_pending(void); /* -1 when no request is waiting */
const char *prospector_input_mode_name(int mode);
int prospector_input_mode_request(int mode);
void prospector_input_mode_format_layer(uint8_t layer, char *buffer, size_t size);
