/* SPDX-License-Identifier: MIT
 * Diagnostic-only wrappers for the pinned Zephyr host. Original timeouts,
 * payloads and return values are preserved. No heap, retry, or packet logging.
 * Completion is local host notification completion, NOT peer application ACK.
 */
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/net/buf.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <prospector/pipeline_profile.h>

LOG_MODULE_REGISTER(bt_tx_profile, LOG_LEVEL_INF);
BUILD_ASSERT(CONFIG_BT_MAX_CONN == 1, "Trace callback tokens require one peripheral peer");
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_EATT), "Trace assumes pinned unenhanced ATT");
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_GATT_NOTIFY_MULTIPLE), "Trace assumes individual notifications");

#define TRACE_SLOTS 32
enum duration_stage { DUR_CALL, DUR_ALLOC, DUR_SEND, DUR_FLIGHT, DUR_TOTAL, DUR_COUNT };
struct duration_stats { uint64_t sum; uint32_t n, bins[8], max; };
struct trace_token {
    bool used, returned, completed, sync, submitted;
    uint32_t started, send_started, started_ms, send_started_ms;
};
struct trace_counts {
    uint32_t attempts, accepted, errors, no_submit, done, early, skipped;
    uint32_t alloc_fail, send_fail, peak;
};
static struct k_spinlock trace_lock;
static struct duration_stats durations[DUR_COUNT];
static struct trace_counts counts;
static struct trace_token tokens[TRACE_SLOTS];
static uint32_t outstanding;
/* Only the serial split service thread sets this scope. Other threads' ATT
 * traffic must not be attributed to its notification even during preemption. */
static k_tid_t scope_thread;
static struct trace_token *scope;

static uint32_t elapsed(uint32_t cycles, uint32_t ms) {
    uint32_t delta_ms = k_uptime_get_32() - ms;
    return delta_ms >= 1000 ? (uint32_t)MIN((uint64_t)delta_ms * 1000U, UINT32_MAX) :
                             pipe_elapsed(cycles);
}

static void duration_add(enum duration_stage stage, uint32_t us) {
    static const uint32_t edges[] = {100, 1000, 6000, 10000, 20000, 30000, 50000};
    struct duration_stats *s = &durations[stage];
    unsigned int bin = 0;
    while (bin < 7 && us >= edges[bin]) bin++;
    s->n++; s->sum += us; s->bins[bin]++; s->max = MAX(s->max, us);
}

static struct trace_token *current_scope(void) {
    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    struct trace_token *t = k_current_get() == scope_thread ? scope : NULL;
    k_spin_unlock(&trace_lock, key);
    return t;
}

/* att.c -> l2cap.c is an external call in the pinned, non-LTO build. The real
 * allocator still receives its original pool, reserve and timeout verbatim. */
extern struct net_buf *__real_bt_l2cap_create_pdu_timeout(struct net_buf_pool *, size_t,
                                                        k_timeout_t);
struct net_buf *__wrap_bt_l2cap_create_pdu_timeout(struct net_buf_pool *pool, size_t reserve,
                                                k_timeout_t timeout) {
    struct trace_token *t = current_scope();
    uint32_t started = pipe_now(), ms = k_uptime_get_32();
    struct net_buf *buf = __real_bt_l2cap_create_pdu_timeout(pool, reserve, timeout);
    if (t) {
        uint32_t us = elapsed(started, ms);
        k_spinlock_key_t key = k_spin_lock(&trace_lock);
        duration_add(DUR_ALLOC, us);
        counts.alloc_fail += buf == NULL;
        k_spin_unlock(&trace_lock, key);
    }
    return buf;
}

extern int __real_bt_att_send(struct bt_conn *, struct net_buf *);
int __wrap_bt_att_send(struct bt_conn *conn, struct net_buf *buf) {
    struct trace_token *t = current_scope();
    uint32_t started = pipe_now(), ms = k_uptime_get_32();
    if (t) {
        k_spinlock_key_t key = k_spin_lock(&trace_lock);
        t->submitted = true; t->send_started = started; t->send_started_ms = ms;
        k_spin_unlock(&trace_lock, key);
    }
    int result = __real_bt_att_send(conn, buf);
    if (t) {
        uint32_t us = elapsed(started, ms);
        k_spinlock_key_t key = k_spin_lock(&trace_lock);
        duration_add(DUR_SEND, us); counts.send_fail += result != 0;
        k_spin_unlock(&trace_lock, key);
    }
    return result;
}

static void notify_done(struct bt_conn *conn, void *user_data) {
    struct trace_token *t = user_data;
    ARG_UNUSED(conn);
    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    /* Tokens are never recycled merely because a connection disconnects:
     * the host may discard a buffer without a callback. Exhaustion falls back
     * to ordinary notify instead of losing input or reusing live user_data. */
    if (!t->used || t->completed) { k_spin_unlock(&trace_lock, key); return; }
    duration_add(DUR_TOTAL, elapsed(t->started, t->started_ms));
    if (t->submitted) duration_add(DUR_FLIGHT, elapsed(t->send_started, t->send_started_ms));
    counts.done++; counts.early += !t->returned;
    bool sync = t->sync;
    t->completed = true;
    if (t->returned) { t->used = false; outstanding--; }
    k_spin_unlock(&trace_lock, key);
    if (sync) pipe_cadence(PIPE_CADENCE_DONE);
}

int pipe_bt_notify(bool sync, const struct bt_gatt_attr *attr, const void *data, uint16_t len) {
    uint32_t started = pipe_now(), ms = k_uptime_get_32();
    struct trace_token *t = NULL;
    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    counts.attempts++;
    for (unsigned int i = 0; i < TRACE_SLOTS; i++) {
        if (!tokens[i].used) {
            t = &tokens[i];
            *t = (struct trace_token){.used = true, .sync = sync,
                                    .started = started, .started_ms = ms};
            outstanding++; counts.peak = MAX(counts.peak, outstanding);
            break;
        }
    }
    if (!t) counts.skipped++;
    scope_thread = k_current_get(); scope = t;
    k_spin_unlock(&trace_lock, key);
    struct bt_gatt_notify_params params = {
        .attr = attr, .data = data, .len = len,
        .func = t ? notify_done : NULL, .user_data = t,
    };
    int result = bt_gatt_notify_cb(NULL, &params);
    uint32_t us = elapsed(started, ms);
    key = k_spin_lock(&trace_lock);
    scope = NULL; scope_thread = NULL;
    duration_add(DUR_CALL, us);
    counts.accepted += result == 0; counts.errors += result != 0;
    if (t) {
        counts.no_submit += !result && !t->submitted;
        t->returned = true;
        if (result || !t->submitted || t->completed) { t->used = false; outstanding--; }
    }
    k_spin_unlock(&trace_lock, key);
    return result;
}

void pipe_bt_report(void) {
    struct duration_stats d[DUR_COUNT];
    struct trace_counts c;
    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    memcpy(d, durations, sizeof(d)); memset(durations, 0, sizeof(durations));
    c = counts; memset(&counts, 0, sizeof(counts));
    uint32_t live = outstanding;
    counts.peak = live;
    k_spin_unlock(&trace_lock, key);
    LOG_INF("BTSTAT att=%u attempt=%u accepted=%u err=%u no_submit=%u done=%u live=%u peak=%u early=%u skip=%u alloc_fail=%u send_fail=%u",
            CONFIG_BT_ATT_TX_COUNT, c.attempts, c.accepted, c.errors, c.no_submit, c.done, live, c.peak,
            c.early, c.skipped, c.alloc_fail, c.send_fail);
    static const char *const names[] = {"CALL", "ALLOC", "SEND", "FLIGHT", "TOTAL"};
    for (unsigned int i = 0; i < DUR_COUNT; i++) {
        const struct duration_stats *s = &d[i];
        LOG_INF("BTT stage=%s n=%u avg_us=%u max_us=%u lt100=%u b100_1=%u b1_6=%u b6_10=%u b10_20=%u b20_30=%u b30_50=%u ge50=%u",
                names[i], s->n, s->n ? (uint32_t)(s->sum / s->n) : 0, s->max,
                s->bins[0], s->bins[1], s->bins[2], s->bins[3], s->bins[4], s->bins[5],
                s->bins[6], s->bins[7]);
    }
}
