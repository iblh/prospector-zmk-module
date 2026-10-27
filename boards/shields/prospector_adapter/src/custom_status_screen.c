#include <lvgl.h>

#if defined(CONFIG_PROSPECTOR_TOUCH_UI)

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <lvgl_mem.h>
#include <prospector/status_layout.h>

LOG_MODULE_REGISTER(prospector_layout, LOG_LEVEL_INF);

/* Keep widget instances alive: their ZMK listeners retain LVGL object pointers. */
lv_obj_t *prospector_screen_classic(void);
lv_obj_t *prospector_screen_field(void);
lv_obj_t *prospector_screen_operator(void);
void prospector_field_animation_set_active(bool active);

static lv_obj_t *screens[PROSPECTOR_LAYOUT_SLOT_COUNT];
static const uint8_t layout_order[PROSPECTOR_LAYOUT_COUNT] = {
    PROSPECTOR_LAYOUT_CLASSIC, PROSPECTOR_LAYOUT_FIELD, PROSPECTOR_LAYOUT_OPERATOR,
};
static uint8_t active_layout = PROSPECTOR_LAYOUT_OPERATOR;
static uint8_t saved_layout = PROSPECTOR_LAYOUT_OPERATOR;

static void log_heap(const char *stage) {
    struct sys_memory_stats memory = {0};
    lvgl_heap_stats(&memory);
    LOG_INF("%s: LVGL heap used %u, free %u, peak %u bytes", stage,
            (unsigned int)memory.allocated_bytes, (unsigned int)memory.free_bytes,
            (unsigned int)memory.max_allocated_bytes);
}

static int layout_settings_set(const char *name, size_t len,
                               settings_read_cb read_cb, void *cb_arg) {
    uint8_t value;
    if (strcmp(name, "selected") != 0) {
        return -ENOENT;
    }
    if (len != sizeof(value) || read_cb(cb_arg, &value, sizeof(value)) != sizeof(value) ||
        value >= PROSPECTOR_LAYOUT_SLOT_COUNT) {
        return -EINVAL;
    }
    if (value == 1) {
        value = PROSPECTOR_LAYOUT_OPERATOR; /* Retire an old Radii preference. */
    }
    saved_layout = value;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(prospector_layout, "prospector_layout", NULL,
                               layout_settings_set, NULL, NULL);

static void save_layout_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (active_layout != saved_layout &&
        settings_save_one("prospector_layout/selected", &active_layout,
                          sizeof(active_layout)) == 0) {
        saved_layout = active_layout;
    }
}
K_WORK_DELAYABLE_DEFINE(save_layout_work, save_layout_work_handler);

lv_obj_t *zmk_display_status_screen(void) {
    /* Boot the previously verified Operator first. Other layouts are created
     * on first visit, after ZMK has already loaded a working screen. */
    screens[PROSPECTOR_LAYOUT_OPERATOR] = prospector_screen_operator();
    log_heap("Operator ready");
    return screens[PROSPECTOR_LAYOUT_OPERATOR];
}

void prospector_status_layout_cycle(int direction) {
    if (direction == 0 || !screens[active_layout]) {
        return;
    }
    unsigned int index = 0;
    while (index < PROSPECTOR_LAYOUT_COUNT && layout_order[index] != active_layout) {
        index++;
    }
    if (index == PROSPECTOR_LAYOUT_COUNT) {
        return;
    }
    uint8_t next = layout_order[(index + PROSPECTOR_LAYOUT_COUNT +
                                (direction > 0 ? 1 : -1)) % PROSPECTOR_LAYOUT_COUNT];
    if (!screens[next]) {
        switch (next) {
        case PROSPECTOR_LAYOUT_CLASSIC:
            screens[next] = prospector_screen_classic();
            break;
        case PROSPECTOR_LAYOUT_FIELD:
            screens[next] = prospector_screen_field();
            break;
        default:
            return;
        }
        log_heap("Layout created");
        if (!screens[next]) {
            LOG_ERR("Layout %u creation failed", next);
            return;
        }
    }
    prospector_field_animation_set_active(next == PROSPECTOR_LAYOUT_FIELD);
    lv_screen_load(screens[next]);
    active_layout = next;
    (void)k_work_reschedule(&save_layout_work, K_MSEC(1200));
}

#elif defined(CONFIG_PROSPECTOR_STATUS_SCREEN_CLASSIC)
#include "layouts/classic/status_screen.c"
#elif defined(CONFIG_PROSPECTOR_STATUS_SCREEN_RADII)
#include "layouts/radii/status_screen.c"
#elif defined(CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD)
#include "layouts/field/status_screen.c"
#elif defined(CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR)
#include "layouts/operator/status_screen.c"
#else
#error "No status screen layout selected"
#endif
