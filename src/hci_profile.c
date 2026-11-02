/* SPDX-License-Identifier: MIT
 * Pinned integrated-controller entry points. Preserve original buffers and
 * return values. Counts ALL ACL PDUs, not just mouse/sync or host application ACKs.
 */
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <prospector/pipeline_profile.h>

LOG_MODULE_REGISTER(hci_profile, LOG_LEVEL_INF);
BUILD_ASSERT(CONFIG_BT_MAX_CONN == 1, "HCI diagnostic is right peripheral only");
#define HCI_SLOTS 16
struct hci_stamp { uint16_t handle; uint32_t cycles, ms, id; bool used; };
struct hci_stats {
    uint32_t tx, err, events, completed, matched, orphan, overflow, reset, peak, batch;
    uint32_t bins[8], max_us;
    uint64_t sum_us;
};
static struct k_spinlock hci_lock;
static struct hci_stamp pending[HCI_SLOTS];
static struct hci_stats stats;
static uint32_t live, sequence;

static uint32_t stamp_age(const struct hci_stamp *s) {
    uint32_t ms = k_uptime_get_32() - s->ms;
    return ms >= 1000 ? (uint32_t)MIN((uint64_t)ms * 1000U, UINT32_MAX) : pipe_elapsed(s->cycles);
}

extern int __real_hci_acl_handle(struct net_buf *, struct net_buf **);
int __wrap_hci_acl_handle(struct net_buf *buf, struct net_buf **evt) {
    /* Capture BEFORE original enqueue: completion may preempt the return. */
    struct hci_stamp *slot = NULL;
    uint32_t id = 0;
    if (buf->len >= sizeof(struct bt_hci_acl_hdr)) {
        uint16_t handle = sys_get_le16(buf->data) & 0x0fff;
        k_spinlock_key_t key = k_spin_lock(&hci_lock);
        stats.tx++;
        for (unsigned int i = 0; i < HCI_SLOTS; i++) {
            if (!pending[i].used) {
                slot = &pending[i]; id = ++sequence;
                *slot = (struct hci_stamp){.used = true, .handle = handle, .id = id,
                                         .cycles = pipe_now(), .ms = k_uptime_get_32()};
                live++; stats.peak = MAX(stats.peak, live);
                break;
            }
        }
        if (!slot) stats.overflow++;
        k_spin_unlock(&hci_lock, key);
    }
    int result = __real_hci_acl_handle(buf, evt);
    if (result) {
        k_spinlock_key_t key = k_spin_lock(&hci_lock);
        stats.err++;
        if (slot && slot->used && slot->id == id) { slot->used = false; live--; }
        k_spin_unlock(&hci_lock, key);
    }
    return result;
}

extern void __real_hci_num_cmplt_encode(struct net_buf *, uint16_t, uint8_t);
void __wrap_hci_num_cmplt_encode(struct net_buf *buf, uint16_t handle, uint8_t num) {
    k_spinlock_key_t key = k_spin_lock(&hci_lock);
    stats.events++; stats.completed += num; stats.batch = MAX(stats.batch, num);
    for (unsigned int n = 0; n < num; n++) {
        struct hci_stamp *oldest = NULL;
        for (unsigned int i = 0; i < HCI_SLOTS; i++) {
            if (pending[i].used && pending[i].handle == handle &&
                (!oldest || (int32_t)(pending[i].id - oldest->id) < 0)) oldest = &pending[i];
        }
        if (!oldest) { stats.orphan++; continue; }
        uint32_t us = stamp_age(oldest);
        static const uint32_t edges[] = {1000, 6000, 10000, 14000, 20000, 30000, 50000};
        unsigned int bin = 0;
        while (bin < 7 && us >= edges[bin]) bin++;
        stats.bins[bin]++; stats.matched++; stats.sum_us += us; stats.max_us = MAX(stats.max_us, us);
        oldest->used = false; live--;
    }
    k_spin_unlock(&hci_lock, key);
    if (num) pipe_cadence(PIPE_CADENCE_HCI_EVT); /* per event, NOT per completed packet */
    __real_hci_num_cmplt_encode(buf, handle, num);
}

static void hci_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(conn); ARG_UNUSED(reason);
    k_spinlock_key_t key = k_spin_lock(&hci_lock);
    memset(pending, 0, sizeof(pending)); live = 0; stats.reset++;
    k_spin_unlock(&hci_lock, key);
}
BT_CONN_CB_DEFINE(hci_trace_cb) = { .disconnected = hci_disconnected };

void pipe_hci_report(void) {
    struct hci_stats s;
    k_spinlock_key_t key = k_spin_lock(&hci_lock);
    s = stats; memset(&stats, 0, sizeof(stats));
    uint32_t outstanding = live;
    stats.peak = live;
    k_spin_unlock(&hci_lock, key);
    LOG_INF("HCI version=8 tx=%u err=%u events=%u completed=%u matched=%u pending=%u peak=%u batch=%u orphan=%u overflow=%u reset=%u avg_us=%u max_us=%u lt1=%u b1_6=%u b6_10=%u b10_14=%u b14_20=%u b20_30=%u b30_50=%u ge50=%u",
            s.tx, s.err, s.events, s.completed, s.matched, outstanding, s.peak, s.batch,
            s.orphan, s.overflow, s.reset, s.matched ? (uint32_t)(s.sum_us / s.matched) : 0,
            s.max_us, s.bins[0], s.bins[1], s.bins[2], s.bins[3], s.bins[4], s.bins[5], s.bins[6], s.bins[7]);
}
