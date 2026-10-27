#include "battery_label.h"

#include <stdio.h>
#include <zmk/display.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/event_manager.h>

#include <fonts.h>
#ifdef CONFIG_PROSPECTOR_TOUCH_UI
#include <prospector/peripheral_state.h>
#endif

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static uint8_t battery_levels[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static bool connected[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];

static void update_label_text(lv_obj_t *label) {
    static char buf[32];
    char *ptr = buf;

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        if (i > 0) {
            *ptr++ = '/';
        }
        if (connected[i]) {
            ptr += snprintf(ptr, sizeof(buf) - (ptr - buf), "%d", battery_levels[i]);
        } else {
            *ptr++ = '-';
        }
    }
    *ptr = '\0';

    lv_label_set_text(label, buf);
}

static void refresh_all_widgets(void) {
    struct zmk_widget_battery_label *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        update_label_text(widget->obj);
    }
}

#ifndef CONFIG_PROSPECTOR_TOUCH_UI
struct battery_state {
    uint8_t source;
    uint8_t level;
};

static void battery_update_cb(struct battery_state state) {
    if (state.source < ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        battery_levels[state.source] = state.level;
        refresh_all_widgets();
    }
}

static struct battery_state get_battery_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev) {
        return (struct battery_state){.source = ev->source, .level = ev->state_of_charge};
    }
    return (struct battery_state){.source = 0, .level = 0};
}


struct connection_state {
    uint8_t source;
    bool connected;
};

static void connection_update_cb(struct connection_state state) {
    if (state.source < ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
        connected[state.source] = state.connected;
        refresh_all_widgets();
    }
}

static struct connection_state get_connection_state(const zmk_event_t *eh) {
    const struct zmk_split_central_status_changed *ev =
        as_zmk_split_central_status_changed(eh);
    if (ev) {
        return (struct connection_state){.source = ev->slot, .connected = ev->connected};
    }
    return (struct connection_state){.source = 0, .connected = false};
}

ZMK_DISPLAY_WIDGET_LISTENER(field_widget_battery_label_battery, struct battery_state,
                            battery_update_cb, get_battery_state);
ZMK_SUBSCRIPTION(field_widget_battery_label_battery, zmk_peripheral_battery_state_changed);

ZMK_DISPLAY_WIDGET_LISTENER(field_widget_battery_label_connection, struct connection_state,
                            connection_update_cb, get_connection_state);
ZMK_SUBSCRIPTION(field_widget_battery_label_connection, zmk_split_central_status_changed);

#endif

#ifdef CONFIG_PROSPECTOR_TOUCH_UI
static struct prospector_peripheral_snapshot peripheral_get_state(const zmk_event_t *event) {
    ARG_UNUSED(event);
    struct prospector_peripheral_snapshot state;
    prospector_peripheral_snapshot_get(&state);
    return state; /* Also hydrate late-created layouts; never reset on NULL. */
}
static void peripheral_update_cb(struct prospector_peripheral_snapshot state) {
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        connected[i] = state.connected[i];
        battery_levels[i] = state.battery[i];
    }
    refresh_all_widgets();
}
ZMK_DISPLAY_WIDGET_LISTENER(field_widget_battery_label_snapshot, struct prospector_peripheral_snapshot,
                            peripheral_update_cb, peripheral_get_state);
ZMK_SUBSCRIPTION(field_widget_battery_label_snapshot, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(field_widget_battery_label_snapshot, zmk_split_central_status_changed);
#endif

int zmk_widget_battery_label_init(struct zmk_widget_battery_label *widget, lv_obj_t *parent) {
    widget->obj = lv_label_create(parent);

    lv_obj_set_style_text_font(widget->obj, &FR_Regular_30, 0);
    lv_obj_set_style_text_letter_space(widget->obj, -1, 0);
    lv_obj_set_style_text_color(widget->obj, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, 0);

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        battery_levels[i] = 0;
        connected[i] = false;
    }

    sys_slist_append(&widgets, &widget->node);

#ifdef CONFIG_PROSPECTOR_TOUCH_UI
    field_widget_battery_label_snapshot_init();
#else
    field_widget_battery_label_battery_init();
    field_widget_battery_label_connection_init();
#endif

    update_label_text(widget->obj);

    return 0;
}

lv_obj_t *zmk_widget_battery_label_obj(struct zmk_widget_battery_label *widget) {
    return widget->obj;
}
