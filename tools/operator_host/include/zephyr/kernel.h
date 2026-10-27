#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define K_FOREVER 0
#define K_NO_WAIT 0
#define K_MSEC(ms) (ms)

struct k_mutex {
    int unused;
};

#define K_MUTEX_DEFINE(name) struct k_mutex name = {0}
static inline void k_mutex_lock(struct k_mutex *mutex, int timeout) {
    (void)mutex;
    (void)timeout;
}
static inline void k_mutex_unlock(struct k_mutex *mutex) {
    (void)mutex;
}

struct k_work {
    void (*handler)(struct k_work *work);
};

struct k_work_delayable {
    struct k_work work;
};

#define K_WORK_DEFINE(name, cb) struct k_work name = {.handler = cb}
#define K_WORK_DELAYABLE_DEFINE(name, cb) struct k_work_delayable name = {.work = {.handler = cb}}

static inline void k_work_init_delayable(struct k_work_delayable *work,
                                         void (*handler)(struct k_work *work)) {
    work->work.handler = handler;
    if (handler) {
        handler(&work->work);
    }
}

static inline int k_work_schedule(struct k_work_delayable *work, int delay) {
    (void)delay;
    if (work->work.handler) {
        work->work.handler(&work->work);
    }
    return 0;
}

static inline int k_work_reschedule(struct k_work_delayable *work, int delay) {
    return k_work_schedule(work, delay);
}

static inline int k_work_cancel_delayable(struct k_work_delayable *work) {
    (void)work;
    return 0;
}

typedef struct sys_snode {
    struct sys_snode *next;
} sys_snode_t;

typedef struct {
    sys_snode_t *head;
    sys_snode_t *tail;
} sys_slist_t;

#define SYS_SLIST_STATIC_INIT(ptr) {NULL, NULL}

static inline bool sys_slist_is_empty(sys_slist_t *list) {
    return list->head == NULL;
}

static inline void sys_slist_append(sys_slist_t *list, sys_snode_t *node) {
    node->next = NULL;
    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
}

#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define SYS_SLIST_FOR_EACH_CONTAINER(list, var, member) \
    for (sys_snode_t *_node = (list)->head; \
         _node != NULL && ((var) = CONTAINER_OF(_node, __typeof__(*(var)), member), true); \
         _node = _node->next)

#define SYS_INIT(fn, level, priority)
#define APPLICATION 0
#define CONFIG_APPLICATION_INIT_PRIORITY 0
#define IS_ENABLED(config_macro) IS_ENABLED1(config_macro)
#define IS_ENABLED1(config_macro) IS_ENABLED2(_XXXX##config_macro)
#define _XXXX1 _YYYY,
#define IS_ENABLED2(one_or_two_args) IS_ENABLED3(one_or_two_args 1, 0)
#define IS_ENABLED3(ignore_this, val, ...) val

uint32_t lv_tick_get(void);
static inline uint32_t k_uptime_get_32(void) { return lv_tick_get(); }

#define K_SECONDS(x) ((x)*1000)
static inline uint32_t k_cycle_get_32(void) { return lv_tick_get() * 1000; }
static inline uint32_t k_cyc_to_us_floor32(uint32_t t) { return t; }

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif
