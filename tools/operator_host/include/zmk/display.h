#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

static inline bool zmk_display_is_initialized(void) {
    return true;
}

static inline struct k_work_q *zmk_display_work_q(void) {
    return (struct k_work_q *)0;
}

static inline int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work) {
    (void)queue;
    if (work && work->handler) {
        work->handler(work);
    }
    return 0;
}

static inline int k_work_reschedule_for_queue(struct k_work_q *queue,
                                             struct k_work_delayable *work, int delay) {
    (void)queue;
    return k_work_reschedule(work, delay);
}

#define ZMK_DISPLAY_WIDGET_LISTENER(listener, state_type, cb, state_func) \
    static void listener##_init(void) { cb(state_func(NULL)); } \
    int listener##_cb(const zmk_event_t *eh) { cb(state_func(eh)); return ZMK_EV_EVENT_BUBBLE; }
