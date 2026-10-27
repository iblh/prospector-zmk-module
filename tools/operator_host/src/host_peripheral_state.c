/* Host-only BLE/core data provider. The snapshot query and all widget C are real. */
#include <prospector/peripheral_state.h>
#include <zmk/event_manager.h>
static bool connected[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static uint8_t batteries[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
bool prospector_split_peripheral_connected(uint8_t slot) {
    return slot < ZMK_SPLIT_BLE_PERIPHERAL_COUNT && connected[slot];
}
int zmk_split_central_get_peripheral_battery_level(uint8_t slot, uint8_t *level) {
    if (slot >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT) { return -1; }
    *level = batteries[slot]; return 0;
}
void host_peripheral_set(uint8_t slot, bool value, uint8_t level) {
    if (slot < ZMK_SPLIT_BLE_PERIPHERAL_COUNT) { connected[slot] = value; batteries[slot] = level; }
}
int operator_widget_battery_circles_snapshot_cb(const zmk_event_t *);
#ifdef PROSPECTOR_HOST_ALL_LAYOUTS
int classic_widget_battery_bar_snapshot_cb(const zmk_event_t *);
int field_widget_battery_label_snapshot_cb(const zmk_event_t *);
#endif
void host_peripheral_publish(void) {
    operator_widget_battery_circles_snapshot_cb(NULL);
#ifdef PROSPECTOR_HOST_ALL_LAYOUTS
    classic_widget_battery_bar_snapshot_cb(NULL);
    field_widget_battery_label_snapshot_cb(NULL);
#endif
}
