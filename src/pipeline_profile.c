/* SPDX-License-Identifier: MIT
 * Aggregate only: no key positions, text, BLE addresses or report payloads.
 * Keep the reporting thread separate from the queue being measured.
 */
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/conn.h>
#include <string.h>
#include <prospector/pipeline_profile.h>

LOG_MODULE_REGISTER(pipe_profile, LOG_LEVEL_INF);
static struct k_spinlock lock;
static uint32_t metrics[PIPE_METRIC_COUNT];
static int64_t heartbeat_due;

#if defined(CONFIG_PROSPECTOR_PIPELINE_CADENCE)
struct cadence_stats {
    uint32_t frames, bins[8], max_us, max_under50_us;
};
static struct cadence_stats cadence[PIPE_CADENCE_COUNT];
static uint32_t cadence_last_cycles[PIPE_CADENCE_COUNT], cadence_last_ms[PIPE_CADENCE_COUNT];
static bool cadence_seen[PIPE_CADENCE_COUNT];

void pipe_cadence(enum pipe_cadence_stage stage) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    uint32_t now = k_cycle_get_32(), ms = k_uptime_get_32();
    struct cadence_stats *s = &cadence[stage];
    s->frames++;
    if (cadence_seen[stage]) {
        uint32_t elapsed_ms = ms - cadence_last_ms[stage];
        /* Cycle counter can wrap during long idle. Such intervals belong to
         * the idle-inclusive >=50ms bucket; millisecond precision suffices. */
        uint32_t delta = elapsed_ms >= 1000 ?
            (uint32_t)MIN((uint64_t)elapsed_ms * 1000U, UINT32_MAX) :
            k_cyc_to_us_floor32(now - cadence_last_cycles[stage]);
        static const uint32_t edges[] = {1000, 6000, 10000, 14000, 20000, 30000, 50000};
        unsigned int bin = 0;
        while (bin < 7 && delta >= edges[bin]) bin++;
        s->bins[bin]++;
        s->max_us = MAX(s->max_us, delta);
        if (delta < 50000) s->max_under50_us = MAX(s->max_under50_us, delta);
    }
    cadence_seen[stage] = true;
    cadence_last_cycles[stage] = now; cadence_last_ms[stage] = ms;
    k_spin_unlock(&lock, key);
}

static void report_cadence(unsigned int stage, const struct cadence_stats *s) {
    static const char *const names[] = {"RX", "USB", "PMW", "TX", "DONE", "HCI_EVT"};
    LOG_INF("CAD stage=%s frames=%u lt1=%u b1_6=%u b6_10=%u b10_14=%u b14_20=%u b20_30=%u b30_50=%u ge50=%u max_us=%u max_under50_us=%u",
            names[stage], s->frames, s->bins[0], s->bins[1],
            s->bins[2], s->bins[3], s->bins[4], s->bins[5], s->bins[6], s->bins[7],
            s->max_us, s->max_under50_us);
}
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
static int keycode_observer(const zmk_event_t *event) {
    const struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(event);
    if (key) pipe_count(key->state ? PIPE_KEYCODE_DOWN : PIPE_KEYCODE_UP);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(pipe_keycode_observer, keycode_observer);
ZMK_SUBSCRIPTION(pipe_keycode_observer, zmk_keycode_state_changed);
#endif

void pipe_count(enum pipe_metric m) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    metrics[m]++;
    k_spin_unlock(&lock, key);
}
void pipe_max(enum pipe_metric m, uint32_t v) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    metrics[m] = MAX(metrics[m], v);
    k_spin_unlock(&lock, key);
}

static void heartbeat(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(heartbeat_work, heartbeat);
static void heartbeat(struct k_work *work) {
    int64_t now = k_uptime_get();
    if (heartbeat_due) pipe_max(PIPE_SYS_LATE_MS, MAX(0, now - heartbeat_due));
    heartbeat_due = now + 100;
    k_work_reschedule(&heartbeat_work, K_MSEC(100));
}

static void report_link(struct bt_conn *conn, void *data) {
    struct bt_conn_info i;
    if (!bt_conn_get_info(conn, &i) && i.type == BT_CONN_TYPE_LE &&
        i.state == BT_CONN_STATE_CONNECTED) {
        LOG_INF("LINK slot_index=%u interval_us=%u latency=%u",
                bt_conn_index(conn), (unsigned)i.le.interval * 1250U, i.le.latency);
    }
}

static void report_thread(void *a, void *b, void *c) {
    k_work_reschedule(&heartbeat_work, K_NO_WAIT);
    while (true) {
#if defined(CONFIG_PROSPECTOR_SCAN_AB)
        for (unsigned int tick = 0; tick < 40; tick++) {
            pipe_scan_tick();
            k_msleep(50);
        }
#else
        k_msleep(2000);
#endif
        uint32_t m[PIPE_METRIC_COUNT];
#if defined(CONFIG_PROSPECTOR_PIPELINE_CADENCE)
        struct cadence_stats cadence_snapshot[PIPE_CADENCE_COUNT];
#endif
        k_spinlock_key_t key = k_spin_lock(&lock);
        memcpy(m, metrics, sizeof(m));
        memset(metrics, 0, sizeof(metrics));
#if defined(CONFIG_PROSPECTOR_PIPELINE_CADENCE)
        memcpy(cadence_snapshot, cadence, sizeof(cadence_snapshot));
        memset(cadence, 0, sizeof(cadence));
#endif
        k_spin_unlock(&lock, key);
        LOG_INF("RX key0=%u key1=%u point=%u other=%u drop_key=%u drop_point=%u drop_other=%u high=%u age_us=%u handle_us=%u handle_err=%u sys_late_ms=%u",
                m[PIPE_RX_KEY0], m[PIPE_RX_KEY1], m[PIPE_RX_POINT], m[PIPE_RX_OTHER],
                m[PIPE_DROP_KEY], m[PIPE_DROP_POINT], m[PIPE_DROP_OTHER], m[PIPE_QUEUE_HIGH],
                m[PIPE_AGE_US], m[PIPE_HANDLE_US], m[PIPE_HANDLE_ERR], m[PIPE_SYS_LATE_MS]);
        LOG_INF("USB key_down=%u key_up=%u key=%u mouse=%u other=%u err=%u sem_timeout=%u wait_us=%u write_us=%u mouse_err=%u mouse_send_us=%u",
                m[PIPE_KEYCODE_DOWN], m[PIPE_KEYCODE_UP], m[PIPE_USB_KEY], m[PIPE_USB_MOUSE], m[PIPE_USB_OTHER], m[PIPE_USB_ERR],
                m[PIPE_USB_TIMEOUT], m[PIPE_USB_WAIT_US], m[PIPE_USB_WRITE_US],
                m[PIPE_MOUSE_SEND_ERR], m[PIPE_MOUSE_SEND_US]);
        LOG_INF("TX key=%u point=%u key_err=%u point_err=%u key_full=%u point_full=%u queue_wait_us=%u notify_us=%u",
                m[PIPE_TX_KEY], m[PIPE_TX_POINT], m[PIPE_TX_KEY_ERR], m[PIPE_TX_POINT_ERR],
                m[PIPE_TX_KEY_FULL], m[PIPE_TX_POINT_FULL], m[PIPE_TX_WAIT_US], m[PIPE_NOTIFY_US]);
        bt_conn_foreach(BT_CONN_TYPE_LE, report_link, NULL);
#if defined(CONFIG_PROSPECTOR_PIPELINE_CADENCE)
        unsigned int first = IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) ? PIPE_CADENCE_RX : PIPE_CADENCE_PMW;
        for (unsigned int i = first; i < first + 2; i++) report_cadence(i, &cadence_snapshot[i]);
#if defined(CONFIG_PROSPECTOR_BLE_TX_TRACE)
        report_cadence(PIPE_CADENCE_DONE, &cadence_snapshot[PIPE_CADENCE_DONE]);
        pipe_bt_report();
#endif
#if defined(CONFIG_PROSPECTOR_HCI_TRACE)
        report_cadence(PIPE_CADENCE_HCI_EVT, &cadence_snapshot[PIPE_CADENCE_HCI_EVT]);
        pipe_hci_report();
#endif
#endif
    }
}
K_THREAD_DEFINE(pipe_profile_thread, 2048, report_thread, NULL, NULL, NULL, 10, 0, 0);
