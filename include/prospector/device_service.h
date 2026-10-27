/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

struct prospector_device_info {
    const char *version;
    const char *build_time;
    const char *source_id;
    const char *keymap;
    const char *usb;
    bool connected[2]; /* BLE slot numbers, not assumed left/right identities. */
};

void prospector_device_info_get(struct prospector_device_info *info);
int prospector_device_request_bootloader(void);
void prospector_device_cancel_bootloader(void);
int prospector_device_bootloader_error(void);
