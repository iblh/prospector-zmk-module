/* SPDX-License-Identifier: MIT
 * Included ONLY by the generated, hash-guarded ZMK central translation unit.
 * Private slot state and is_scanning remain in that single translation unit.
 */
#include <errno.h>
#include <zmk/workqueue.h>
#include <prospector/split_scan_policy.h>

static void split_central_device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                                      struct net_buf_simple *ad);
static enum prospector_scan_mode scan_policy_current = P_SCAN_OFF;
K_MUTEX_DEFINE(scan_policy_mutex);
static void scan_policy_retry_cb(struct k_work *work) {
    (void)work;
    start_scanning();
}
K_WORK_DELAYABLE_DEFINE(scan_policy_retry, scan_policy_retry_cb);
static void scan_policy_retry_later(void) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &scan_policy_retry, K_MSEC(1000));
}

static enum prospector_scan_mode scan_policy_desired(unsigned int *connected) {
    bool connecting = false;
    *connected = 0;
    for (unsigned int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        *connected += peripherals[i].state == PERIPHERAL_SLOT_STATE_CONNECTED;
        connecting |= peripherals[i].state == PERIPHERAL_SLOT_STATE_CONNECTING;
    }
    return prospector_scan_mode(is_enabled, *connected,
                               CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS, connecting);
}

/* Caller holds the nonblocking mutex. Failed stops retain the known old mode. */
static int scan_policy_stop_locked(void) {
    if (!is_scanning) { scan_policy_current = P_SCAN_OFF; return 0; }
    int err = bt_le_scan_stop();
    if (!err || err == -EALREADY) {
        is_scanning = false; scan_policy_current = P_SCAN_OFF;
        return 0;
    }
    return err;
}

static int stop_scanning(void) {
    if (k_mutex_lock(&scan_policy_mutex, K_NO_WAIT)) {
        scan_policy_retry_later();
        return -EAGAIN;
    }
    k_work_cancel_delayable(&scan_policy_retry);
    int err = scan_policy_stop_locked();
    k_mutex_unlock(&scan_policy_mutex);
    if (err) scan_policy_retry_later();
    return err;
}

static int start_scanning(void) {
    /* Never wait for another scanner operation in a Bluetooth/HID callback. */
    if (k_mutex_lock(&scan_policy_mutex, K_NO_WAIT)) {
        scan_policy_retry_later();
        return 0;
    }
    unsigned int connected;
    enum prospector_scan_mode desired = scan_policy_desired(&connected);
    int err = 0;
    if (desired == P_SCAN_OFF || (is_scanning && scan_policy_current != desired))
        err = scan_policy_stop_locked();
    if (!err && desired != P_SCAN_OFF && !is_scanning) {
        struct bt_le_scan_param params = *BT_LE_SCAN_PASSIVE;
        if (desired == P_SCAN_BACKGROUND) {
            params.interval = P_SCAN_BG_INTERVAL;
            params.window = P_SCAN_BG_WINDOW;
        }
        err = bt_le_scan_start(&params, split_central_device_found);
        if (!err) {
            is_scanning = true; scan_policy_current = desired;
            LOG_INF("SCAN_POLICY connected=%u mode=%u interval_us=%u window_us=%u",
                    connected, (unsigned)desired, params.interval * 625U, params.window * 625U);
        } else if (err == -EALREADY) {
            /* Actual parameters are unknown; stop/restart on the retry. */
            is_scanning = true; scan_policy_current = P_SCAN_UNKNOWN;
        }
    }
    unsigned int after;
    bool changed = scan_policy_desired(&after) != desired;
    if (!err && !changed) k_work_cancel_delayable(&scan_policy_retry);
    k_mutex_unlock(&scan_policy_mutex);
    if (err || changed) scan_policy_retry_later();
    return err;
}
