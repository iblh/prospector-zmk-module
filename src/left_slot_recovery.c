/* SPDX-License-Identifier: MIT
 * Explicit maintenance image only. Never enabled in ordinary Touch builds.
 * Keep slot 0 and its bond; remove only the stored slot 1 identity, once.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zmk/workqueue.h>
#include <prospector/left_slot_recovery.h>

BUILD_ASSERT(CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT == 1, "Recovery protects slot 0");
BUILD_ASSERT(CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS == 2, "Recovery requires two slots");
BUILD_ASSERT(CONFIG_BT_ID_MAX == 1, "Recovery targets the default Bluetooth identity");

#define LR_RIGHT_PATH "ble/peripheral_addresses/0"
#define LR_LEFT_PATH "ble/peripheral_addresses/1"
#if CONFIG_PROSPECTOR_LEFT_RECOVERY_GENERATION == 2
#define LR_RECORD_PATH "prospector_left_repair/v2"
#else
#define LR_RECORD_PATH "prospector_left_repair/v1"
#endif
#define LR_RECORD_MAGIC 0x4C523031u
#define LR_PENDING 1
#define LR_COMPLETE 2
#define LR_COMPLETE_EMPTY 3
#define LR_KEY_MAX 256

struct repair_record {
    uint32_t magic;
    uint8_t state;
    bt_addr_le_t right, old_left;
    uint8_t reserved;
};
BUILD_ASSERT(sizeof(struct repair_record) == 20, "Recovery record ABI");

/* Secret key bytes are compared in RAM only; never printed, exported or saved. */
static uint8_t right_key_before[LR_KEY_MAX], right_key_after[LR_KEY_MAX];
static atomic_t started, finished, status;
static int (*resume_transport)(void);
enum result {
    LR_CHECKING, LR_CLEARED, LR_EMPTY, LR_NO_RIGHT, LR_BAD_DATA, LR_CHANGED, LR_STORAGE_ERROR,
    LR_UNPAIR_ERROR, LR_VERIFY_ERROR
};
static const char *const messages[] = {
    "LEFT CHECKING", "LEFT CLEARED", "LEFT SLOT EMPTY", "RIGHT NOT FOUND",
    "BAD SLOT DATA", "SLOT CHANGED", "STORAGE ERROR", "UNPAIR ERROR", "VERIFY ERROR"
};

const char *prospector_left_recovery_status(void) { return messages[atomic_get(&status)]; }

struct read_context {
    void *data;
    size_t capacity, length;
    int error;
    bool found;
};
static int read_exact(const char *key, size_t len, settings_read_cb read_cb,
                      void *arg, void *param) {
    struct read_context *ctx = param;
    if ((key && *key) || !len) { return 0; }
    if (ctx->found || len > ctx->capacity) { ctx->error = -EINVAL; return -EINVAL; }
    ctx->found = true;
    int n = read_cb(arg, ctx->data, len);
    if (n < 0 || (size_t)n != len) { ctx->error = n < 0 ? n : -EIO; return ctx->error; }
    ctx->length = len;
    return 0;
}
/* The pinned settings loader does not propagate callback errors; capture them. */
static int read_value(const char *path, void *data, size_t capacity, size_t *length) {
    struct read_context ctx = {.data = data, .capacity = capacity};
    int err = settings_load_subtree_direct(path, read_exact, &ctx);
    *length = ctx.length;
    return err ? err : ctx.error;
}
static int read_address(const char *path, bt_addr_le_t *addr) {
    size_t length;
    memset(addr, 0, sizeof(*addr));
    int err = read_value(path, addr, sizeof(*addr), &length);
    return err ? err : (length && length != sizeof(*addr) ? -EINVAL : 0);
}
static bool valid_address(const bt_addr_le_t *addr) {
    return addr->type <= BT_ADDR_LE_RANDOM && bt_addr_le_cmp(addr, BT_ADDR_LE_ANY) &&
           bt_addr_le_cmp(addr, BT_ADDR_LE_NONE);
}
/* Matches the pinned Zephyr ID=0 bt/keys encoding; tested against its encoder. */
static void key_path(char *path, size_t size, const bt_addr_le_t *addr) {
    snprintf(path, size, "bt/keys/%02x%02x%02x%02x%02x%02x%u",
             addr->a.val[5], addr->a.val[4], addr->a.val[3],
             addr->a.val[2], addr->a.val[1], addr->a.val[0], addr->type);
}
struct bond_query { const bt_addr_le_t *addr; bool found; };
static void find_bond(const struct bt_bond_info *info, void *param) {
    struct bond_query *query = param;
    query->found |= !bt_addr_le_cmp(&info->addr, query->addr);
}
static bool has_bond(const bt_addr_le_t *addr) {
    struct bond_query query = {.addr = addr};
    bt_foreach_bond(BT_ID_DEFAULT, find_bond, &query);
    return query.found;
}
static int write_record(const struct repair_record *record) {
    int err = settings_save_one(LR_RECORD_PATH, record, sizeof(*record));
    if (err) { return err; }
    struct repair_record saved;
    size_t length;
    err = read_value(LR_RECORD_PATH, &saved, sizeof(saved), &length);
    return err ? err : (length != sizeof(saved) || memcmp(&saved, record, sizeof(saved)) ? -EIO : 0);
}
static void release_transport(enum result result) {
    memset(right_key_before, 0, sizeof(right_key_before));
    memset(right_key_after, 0, sizeof(right_key_after));
    atomic_set(&status, result);
    /* On error, keep discovery gated. The user can restore r2 via the touch UI.
     * Never let a fresh controller pair without a durable one-time decision. */
    if (result != LR_CLEARED && result != LR_EMPTY) { return; }
    atomic_set(&finished, 1);
    resume_transport();
}

static void recover(struct k_work *work) {
    (void)work;
    struct repair_record record = {0};
    size_t length, right_length, after_length;
    bt_addr_le_t right, left, check;
    char right_path[24], left_path[24];

    /* Reads wait for settings_load/commit to release its lock. Scanning is gated. */
    if (read_value(LR_RECORD_PATH, &record, sizeof(record), &length)) {
        release_transport(LR_BAD_DATA); return;
    }
    if (length && (length != sizeof(record) || record.magic != LR_RECORD_MAGIC || record.reserved ||
                   (record.state != LR_PENDING && record.state != LR_COMPLETE && record.state != LR_COMPLETE_EMPTY) ||
                   !valid_address(&record.right) ||
                   (record.state != LR_COMPLETE_EMPTY && (!valid_address(&record.old_left) ||
                    !bt_addr_le_cmp(&record.right, &record.old_left))))) {
        release_transport(LR_BAD_DATA); return;
    }
    if (length && (record.state == LR_COMPLETE || record.state == LR_COMPLETE_EMPTY)) {
        /* Never touch a newly paired left controller on subsequent boots. */
        release_transport(record.state == LR_COMPLETE ? LR_CLEARED : LR_EMPTY); return;
    }
    if (read_address(LR_RIGHT_PATH, &right) || read_address(LR_LEFT_PATH, &left)) {
        release_transport(LR_BAD_DATA); return;
    }
    if (!valid_address(&right) || !has_bond(&right)) {
        release_transport(LR_NO_RIGHT); return;
    }
    if (!length && !bt_addr_le_cmp(&left, BT_ADDR_LE_ANY)) {
        /* Persist even a no-op, or a later boot could mistake the NEW left for old. */
        record = (struct repair_record){.magic = LR_RECORD_MAGIC, .state = LR_COMPLETE_EMPTY, .right = right};
        if (write_record(&record)) { release_transport(LR_STORAGE_ERROR); return; }
        release_transport(LR_EMPTY); return;
    }
    if (!length) {
        if (!valid_address(&left) || !bt_addr_le_cmp(&right, &left)) {
            release_transport(LR_BAD_DATA); return;
        }
        record = (struct repair_record){.magic = LR_RECORD_MAGIC, .state = LR_PENDING,
                                        .right = right, .old_left = left};
    } else if (bt_addr_le_cmp(&record.right, &right) ||
               (bt_addr_le_cmp(&left, BT_ADDR_LE_ANY) && bt_addr_le_cmp(&left, &record.old_left))) {
        /* A pending operation may only resume against its original identity. */
        release_transport(LR_CHANGED); return;
    }
    key_path(right_path, sizeof(right_path), &right);
    key_path(left_path, sizeof(left_path), &record.old_left);
    if (read_value(right_path, right_key_before, sizeof(right_key_before), &right_length) || !right_length) {
        release_transport(LR_NO_RIGHT); return;
    }
    /* Durable identity-bound intent BEFORE any destructive operation. */
    if (write_record(&record)) { release_transport(LR_STORAGE_ERROR); return; }
    if (bt_unpair(BT_ID_DEFAULT, &record.old_left)) { release_transport(LR_UNPAIR_ERROR); return; }
    /* bt_unpair can hide a persistent key-delete error. Verify the backend too. */
    if (has_bond(&record.old_left) ||
        read_value(left_path, right_key_after, sizeof(right_key_after), &after_length) || after_length) {
        release_transport(LR_UNPAIR_ERROR); return;
    }
    if (settings_delete(LR_LEFT_PATH) || read_address(LR_LEFT_PATH, &check) ||
        bt_addr_le_cmp(&check, BT_ADDR_LE_ANY)) {
        release_transport(LR_STORAGE_ERROR); return;
    }
    if (read_address(LR_RIGHT_PATH, &check) || bt_addr_le_cmp(&right, &check) || !has_bond(&right) ||
        read_value(right_path, right_key_after, sizeof(right_key_after), &after_length) ||
        after_length != right_length || memcmp(right_key_before, right_key_after, right_length)) {
        release_transport(LR_VERIFY_ERROR); return;
    }
    record.state = LR_COMPLETE;
    if (write_record(&record)) { release_transport(LR_STORAGE_ERROR); return; }
    atomic_set(&status, LR_CLEARED);
    memset(right_key_before, 0, sizeof(right_key_before));
    memset(right_key_after, 0, sizeof(right_key_after));
    /* Reload the private central address array; never scan with stale RAM state. */
    sys_reboot(SYS_REBOOT_COLD);
}
K_WORK_DELAYABLE_DEFINE(repair_work, recover);

bool prospector_left_recovery_start(int (*resume)(void)) {
    if (atomic_get(&finished)) { return false; }
    if (atomic_cas(&started, 0, 1)) {
        resume_transport = resume;
        /* Let the UF2 bootloader double-reset window expire before our reboot. */
        if (k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &repair_work, K_SECONDS(2)) < 0) {
            atomic_set(&status, LR_STORAGE_ERROR);
            return true; /* No durable decision: keep discovery gated. */
        }
    }
    return true;
}
