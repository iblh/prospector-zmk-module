/* SPDX-License-Identifier: MIT */
#include <prospector/peripheral_state.h>
#include <zmk/split/central.h>

uint8_t prospector_peripheral_display_source(uint8_t display_index) {
    if (display_index >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) { return UINT8_MAX; }
#if ZMK_SPLIT_BLE_PERIPHERAL_COUNT == 2
    return display_index == 0 ? CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT
                             : 1 - CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT;
#else
    return display_index;
#endif
}

void prospector_peripheral_snapshot_get(struct prospector_peripheral_snapshot *snapshot) {
    for (uint8_t i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        uint8_t source = prospector_peripheral_display_source(i);
        snapshot->connected[i] = prospector_split_peripheral_connected(source);
        uint8_t level = 0;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
        (void)zmk_split_central_get_peripheral_battery_level(source, &level);
#endif
        snapshot->battery[i] = level;
    }
}
