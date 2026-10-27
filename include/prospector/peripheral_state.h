/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zmk/ble.h>

struct prospector_peripheral_snapshot {
    bool connected[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
    uint8_t battery[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
};
/* Raw bonded slot numbers are independent of physical left/right identity. */
bool prospector_split_peripheral_connected(uint8_t slot);
uint8_t prospector_peripheral_display_source(uint8_t display_index);
void prospector_peripheral_snapshot_get(struct prospector_peripheral_snapshot *snapshot);
