/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <prospector/input_mode.h>

/* Raw position events and this worker run on the ZMK system work queue.
 * Observe BEFORE hold-tap captures events. Replayed events bypass this wrapper.
 * A normal subscription after keymap would never see handled/captured events.
 * GNU ld --wrap keeps the upstream ZMK checkout unchanged. */
#define QUIET_MS 350 /* longer than the longest (280ms) hold-tap in this keymap */
static atomic_t current_mode;
static atomic_t requested_mode = ATOMIC_INIT(-1);
static uint64_t pressed_positions;
static int64_t last_position_time;

int prospector_input_mode_get(void) { return atomic_get(&current_mode); }
int prospector_input_mode_pending(void) { return atomic_get(&requested_mode); }
const char *prospector_input_mode_name(int mode) {
    static const char *const names[] = {"MAC-DV", "WIN-DV", "GAME"};
    return mode >= 0 && mode < 3 ? names[mode] : "UNKNOWN";
}

static bool hid_is_neutral(void) {
    const struct zmk_hid_keyboard_report_body *keyboard = &zmk_hid_get_keyboard_report()->body;
    const struct zmk_hid_consumer_report_body *consumer = &zmk_hid_get_consumer_report()->body;
    if (keyboard->modifiers || zmk_hid_get_explicit_mods()) { return false; }
    for (size_t i = 0; i < sizeof(keyboard->keys); i++) {
        if (((const uint8_t *)keyboard->keys)[i]) { return false; }
    }
    for (size_t i = 0; i < sizeof(consumer->keys); i++) {
        if (((const uint8_t *)consumer->keys)[i]) { return false; }
    }
    return zmk_hid_get_mouse_report()->body.buttons == 0;
}

static void switch_mode(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(mode_work, switch_mode);
static void switch_mode(struct k_work *work) {
    ARG_UNUSED(work);
    int mode = prospector_input_mode_pending();
    if (mode < 0 || pressed_positions) { return; } /* next release re-arms work */
    if (k_uptime_get() - last_position_time < QUIET_MS || !hid_is_neutral()) {
        k_work_reschedule(&mode_work, K_MSEC(QUIET_MS));
        return;
    }
    if (!atomic_cas(&requested_mode, mode, -1)) {
        k_work_reschedule(&mode_work, K_MSEC(QUIET_MS));
        return;
    }
    atomic_set(&current_mode, mode); /* layer listeners read the new mode */
    zmk_keymap_layer_to(mode, false);
}

int prospector_input_mode_request(int mode) {
    if (mode < 0 || mode > PROSPECTOR_GAME) { return -EINVAL; }
    atomic_set(&requested_mode, mode == prospector_input_mode_get() ? -1 : mode);
    int rc = k_work_reschedule(&mode_work, K_MSEC(QUIET_MS));
    if (rc < 0) { atomic_cas(&requested_mode, mode, -1); }
    return rc < 0 ? rc : 0;
}

int __real_raise_zmk_position_state_changed(struct zmk_position_state_changed event);
int __wrap_raise_zmk_position_state_changed(struct zmk_position_state_changed event) {
    if (event.position < 41) {
        uint64_t mask = UINT64_C(1) << event.position;
        if (event.state) { pressed_positions |= mask; }
        else { pressed_positions &= ~mask; }
        last_position_time = k_uptime_get();
        if (prospector_input_mode_pending() >= 0) {
            k_work_reschedule(&mode_work, K_MSEC(QUIET_MS));
        }
    }
    return __real_raise_zmk_position_state_changed(event);
}

void prospector_input_mode_format_layer(uint8_t layer, char *buffer, size_t size) {
    static const char *const temporary[] = {"NUM", "NAV", "SYM", "FN", "SLOW", "SCRL"};
    if (layer < 3) {
        snprintf(buffer, size, "%s", prospector_input_mode_name(layer));
    } else if (layer < 9) {
        static const char prefix[] = {'M', 'W', 'G'};
        snprintf(buffer, size, "%c/%s", prefix[prospector_input_mode_get()], temporary[layer - 3]);
    } else { snprintf(buffer, size, "%u", layer); }
}
