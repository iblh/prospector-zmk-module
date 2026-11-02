/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/kernel.h>

enum pipe_metric {
    PIPE_RX_KEY0, PIPE_RX_KEY1, PIPE_RX_POINT, PIPE_RX_OTHER,
    PIPE_DROP_KEY, PIPE_DROP_POINT, PIPE_DROP_OTHER,
    PIPE_QUEUE_HIGH, PIPE_AGE_US, PIPE_HANDLE_US, PIPE_HANDLE_ERR,
    PIPE_KEYCODE_DOWN, PIPE_KEYCODE_UP,
    PIPE_USB_KEY, PIPE_USB_MOUSE, PIPE_USB_OTHER, PIPE_USB_ERR,
    PIPE_USB_TIMEOUT, PIPE_USB_WAIT_US, PIPE_USB_WRITE_US,
    PIPE_MOUSE_SEND_ERR, PIPE_MOUSE_SEND_US,
    PIPE_TX_KEY, PIPE_TX_POINT, PIPE_TX_KEY_ERR, PIPE_TX_POINT_ERR,
    PIPE_TX_KEY_FULL, PIPE_TX_POINT_FULL, PIPE_TX_WAIT_US, PIPE_NOTIFY_US,
    PIPE_SYS_LATE_MS, PIPE_METRIC_COUNT
};
enum pipe_cadence_stage {
    PIPE_CADENCE_RX, PIPE_CADENCE_USB, PIPE_CADENCE_PMW, PIPE_CADENCE_TX,
    PIPE_CADENCE_DONE,
    PIPE_CADENCE_HCI_EVT,
    PIPE_CADENCE_COUNT
};
#if defined(CONFIG_PROSPECTOR_PIPELINE_CADENCE)
void pipe_cadence(enum pipe_cadence_stage stage);
#else
static inline void pipe_cadence(enum pipe_cadence_stage stage) { (void)stage; }
#endif
void pipe_count(enum pipe_metric metric);
void pipe_max(enum pipe_metric metric, uint32_t value);
#if defined(CONFIG_PROSPECTOR_HCI_TRACE)
void pipe_hci_report(void);
#endif
#if defined(CONFIG_PROSPECTOR_SCAN_AB)
void pipe_scan_tick(void);
int pipe_scan_apply(bool blocked);
bool pipe_scan_status(unsigned int *links);
#endif
#if defined(CONFIG_PROSPECTOR_BLE_TX_TRACE)
struct bt_gatt_attr;
int pipe_bt_notify(bool sync, const struct bt_gatt_attr *attr, const void *data, uint16_t len);
void pipe_bt_report(void);
#endif
static inline uint32_t pipe_now(void) { return k_cycle_get_32(); }
static inline uint32_t pipe_elapsed(uint32_t start) {
    return k_cyc_to_us_floor32(k_cycle_get_32() - start);
}
