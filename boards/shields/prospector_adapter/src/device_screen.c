/* SPDX-License-Identifier: MIT */
#include <lvgl.h>
#include <fonts.h>
#include <prospector/device_screen.h>
#include <prospector/device_service.h>
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
#include <prospector/input_mode.h>
#endif

#define HOLD_MS 2000
#define CONFIRM_TIMEOUT_MS 15000

enum device_page { PAGE_INFO, PAGE_MODES, PAGE_CONFIRM, PAGE_PENDING };
static enum device_page page;
static lv_obj_t *root, *info_page, *confirm_page;
static lv_obj_t *info_text, *hold_button, *hold_progress, *hold_hint;
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
static int mode_error;
static lv_obj_t *modes_page, *mode_label, *mode_hint, *mode_buttons[3];
#endif
static lv_timer_t *timer;
static lv_indev_t *hold_indev;
static uint32_t hold_started, confirm_started, refreshed;
static bool holding;

static void style_text(lv_obj_t *label) {
    lv_obj_set_style_text_font(label, &FG_Medium_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF0F7FC), 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int y) {
    lv_obj_t *label = lv_label_create(parent);
    style_text(label);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 248);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

static lv_obj_t *make_panel(lv_obj_t *parent) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return panel;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int width,
                             lv_event_cb_t callback) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 38);
    lv_obj_set_pos(button, x, 190);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x299BE8), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1676BB), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, NULL);
    lv_obj_t *label = lv_label_create(button);
    style_text(label);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void reset_hold(void) {
    holding = false;
    hold_indev = NULL;
    lv_bar_set_value(hold_progress, 0, LV_ANIM_OFF);
}

static void refresh_info(void) {
    struct prospector_device_info info;
    prospector_device_info_get(&info);
    lv_label_set_text_fmt(info_text,
        "FW %s\nBUILD %s\nSRC %s\nKEYMAP %s\nUSB %s   K1 %s  K2 %s",
        info.version, info.build_time, info.source_id, info.keymap, info.usb,
        info.connected[0] ? "ON" : "--", info.connected[1] ? "ON" : "--");
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    lv_label_set_text_fmt(mode_label, "MODE %s   >", info.keymap);
    lv_label_set_text_fmt(info_text, "FW %s\nBUILD %s\nSRC %s\n\nUSB %s   K1 %s  K2 %s",
        info.version, info.build_time, info.source_id, info.usb,
        info.connected[0] ? "ON" : "--", info.connected[1] ? "ON" : "--");
#endif
    refreshed = lv_tick_get();
}

static void show_info(void) {
    prospector_device_cancel_bootloader();
    reset_hold();
    page = PAGE_INFO;
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    lv_obj_add_flag(modes_page, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_add_flag(confirm_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(info_page, LV_OBJ_FLAG_HIDDEN);
    refresh_info();
}

static void close_page(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    prospector_device_screen_on_idle();
}

static void cancel_confirmation(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        show_info();
    }
}

#ifdef CONFIG_PROSPECTOR_INPUT_MODES
static void refresh_modes(void) {
    int current = prospector_input_mode_get();
    int pending = prospector_input_mode_pending();
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(mode_buttons[i], lv_color_hex(
            i == pending ? 0xA66A14 : i == current ? 0x168B75 : 0x29404B), 0);
    }
    if (mode_error < 0) {
        lv_label_set_text_fmt(mode_hint, "MODE FAILED (%d)", mode_error);
    } else if (pending >= 0) {
        lv_label_set_text_fmt(mode_hint, "WAIT: %s\nRELEASE ALL KEYS", prospector_input_mode_name(pending));
    } else {
        lv_label_set_text_fmt(mode_hint, "ACTIVE: %s", prospector_input_mode_name(current));
    }
}
static void select_mode(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) { return; }
    int mode = (int)(uintptr_t)lv_event_get_user_data(event);
    mode_error = prospector_input_mode_request(mode);
    refresh_modes();
}
static void open_modes(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || page != PAGE_INFO) { return; }
    page = PAGE_MODES;
    mode_error = 0;
    lv_obj_add_flag(info_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(modes_page, LV_OBJ_FLAG_HIDDEN);
    refresh_modes();
}
#endif

static void open_confirmation(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || page != PAGE_INFO) {
        return;
    }
    reset_hold();
    page = PAGE_CONFIRM;
    confirm_started = lv_tick_get();
    lv_obj_remove_state(hold_button, LV_STATE_DISABLED);
    lv_label_set_text(hold_hint, "HOLD FOR 2 SECONDS");
    lv_obj_add_flag(info_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(confirm_page, LV_OBJ_FLAG_HIDDEN);
    /* CLICKED is emitted on release. Only a new PRESSED event on HOLD can
     * start its timer; do not swallow a genuine quick second contact. */
}

static bool pointer_inside_hold(void) {
    if (!hold_indev || lv_indev_get_state(hold_indev) != LV_INDEV_STATE_PRESSED) {
        return false;
    }
    lv_point_t point;
    lv_area_t area;
    lv_indev_get_point(hold_indev, &point);
    lv_obj_get_coords(hold_button, &area);
    return point.x >= area.x1 && point.x <= area.x2 &&
           point.y >= area.y1 && point.y <= area.y2;
}

static void hold_event(lv_event_t *event) {
    if (page != PAGE_CONFIRM) {
        return;
    }
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        hold_indev = lv_event_get_indev(event);
        holding = hold_indev != NULL;
        hold_started = lv_tick_get();
        break;
    case LV_EVENT_RELEASED:
        reset_hold();
        break;
    case LV_EVENT_PRESS_LOST:
        if (hold_indev) {
            lv_indev_wait_release(hold_indev);
        }
        reset_hold();
        break;
    default:
        break;
    }
}

static void show_failure(int error) {
    reset_hold();
    page = PAGE_CONFIRM;
    confirm_started = lv_tick_get();
    lv_obj_add_state(hold_button, LV_STATE_DISABLED);
    lv_label_set_text_fmt(hold_hint, "FLASH FAILED (%d)", error);
}

static void update_page(lv_timer_t *unused) {
    LV_UNUSED(unused);
    uint32_t now = lv_tick_get();
    if (page == PAGE_INFO) {
        if (now - refreshed >= 500) {
            refresh_info();
        }
    }
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    else if (page == PAGE_MODES) { refresh_modes(); }
#endif
    else if (page == PAGE_PENDING) {
        int error = prospector_device_bootloader_error();
        if (error < 0) {
            show_failure(error);
        }
    } else if (now - confirm_started >= CONFIRM_TIMEOUT_MS) {
        show_info();
    } else if (holding) {
        if (!pointer_inside_hold()) {
            if (hold_indev) {
                lv_indev_wait_release(hold_indev);
            }
            reset_hold();
            return;
        }
        uint32_t elapsed = now - hold_started;
        lv_bar_set_value(hold_progress, elapsed < HOLD_MS ? elapsed : HOLD_MS, LV_ANIM_OFF);
        if (elapsed >= HOLD_MS) {
            holding = false;
            page = PAGE_PENDING; /* Exactly one request per successful hold. */
            lv_obj_add_state(hold_button, LV_STATE_DISABLED);
            lv_label_set_text(hold_hint, "ENTERING NICENANO");
            int error = prospector_device_request_bootloader();
            if (error < 0) {
                show_failure(error);
            }
        }
    }
}

static void create_page(void) {
    root = make_panel(lv_layer_top());
    lv_obj_set_style_bg_color(root, lv_color_hex(0x121A1C), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    info_page = make_panel(root);
    make_label(info_page, "DEVICE", 8);
    info_text = make_label(info_page, "", 42);
    lv_obj_set_style_text_line_space(info_text, 4, 0);
    lv_obj_set_style_text_letter_space(info_text, -1, 0);
    make_button(info_page, "CLOSE", 20, 106, close_page);
    make_button(info_page, "FLASH", 154, 106, open_confirmation);

#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    lv_obj_t *change = make_button(info_page, "", 16, 248, open_modes);
    lv_obj_set_pos(change, 16, 124);
    lv_obj_set_height(change, 27);
    lv_obj_set_style_bg_opa(change, LV_OPA_TRANSP, 0);
    mode_label = lv_obj_get_child(change, 0);
    modes_page = make_panel(root);
    make_label(modes_page, "INPUT MODE", 8);
    for (int i = 0; i < 3; i++) {
        mode_buttons[i] = make_button(modes_page, prospector_input_mode_name(i), 30, 220, select_mode);
        lv_obj_set_y(mode_buttons[i], 38 + i * 39);
        lv_obj_set_height(mode_buttons[i], 33);
        lv_obj_remove_event_cb(mode_buttons[i], select_mode);
        lv_obj_add_event_cb(mode_buttons[i], select_mode, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    mode_hint = make_label(modes_page, "", 153);
    lv_obj_set_style_text_font(mode_hint, LV_FONT_DEFAULT, 0);
    make_button(modes_page, "BACK", 80, 120, cancel_confirmation);
#endif
    confirm_page = make_panel(root);
    make_label(confirm_page, "ENTER UF2?", 8);
    make_label(confirm_page, "KEYBOARD WILL\nDISCONNECT", 46);
    hold_hint = make_label(confirm_page, "", 116);
    hold_progress = lv_bar_create(confirm_page);
    lv_obj_set_size(hold_progress, 240, 8);
    lv_obj_align(hold_progress, LV_ALIGN_TOP_MID, 0, 158);
    lv_bar_set_range(hold_progress, 0, HOLD_MS);
    lv_obj_set_style_bg_color(hold_progress, lv_color_hex(0x324653), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hold_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hold_progress, lv_color_hex(0x299BE8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(hold_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    make_button(confirm_page, "CANCEL", 20, 86, cancel_confirmation);
    hold_button = make_button(confirm_page, "HOLD 2S", 126, 134, hold_event);
    timer = lv_timer_create(update_page, 25, NULL);
    lv_timer_pause(timer);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

void prospector_device_screen_open(void) {
    if (!root) {
        create_page();
    }
    show_info();
    lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    lv_timer_resume(timer);
}

bool prospector_device_screen_is_open(void) {
    return root && !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
}

void prospector_device_screen_on_idle(void) {
    prospector_device_cancel_bootloader();
    if (!root) {
        return;
    }
    reset_hold();
    page = PAGE_INFO;
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
        lv_indev_wait_release(indev);
    }
}
