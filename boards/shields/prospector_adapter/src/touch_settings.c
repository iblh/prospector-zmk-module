/* Prospector Touch settings UI: swipe down for the brightness slider.
 * This deliberately does not replace ZMK's existing status widgets.
 * Target: Zephyr 4.1, LVGL 9, 280Zo Prospector module.
 * Experimental integration code: requires actual Zephyr build + device validation.
 */
#include <lvgl.h>
#include <stdint.h>
#include <prospector/brightness.h>
#include <prospector/status_layout.h>
#include <prospector/touch_settings.h>
#include <prospector/device_screen.h>
#include <fonts.h>
#include <control_style.h>

/* The firmware intentionally has no LVGL theme. Every new control needs its
 * own visible styles; otherwise LVGL defaults to transparent/black on black. */
#define TOUCH_ACCENT PSP_CONTROL_ACCENT
#define TOUCH_TEXT PSP_CONTROL_TEXT

static void style_action_button(lv_obj_t *button) {
    prospector_control_button(button);
}

static void style_action_label(lv_obj_t *label) {
    prospector_control_text(label);
}

struct touch_screen_controls {
    lv_obj_t *settings_panel;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_label;
};
/* One top-layer panel serves every layout; do not allocate three copies. */
static struct touch_screen_controls shared_control;
static unsigned int controls_count;

static void refresh_brightness(struct touch_screen_controls *control) {
    uint8_t value = prospector_brightness_get_manual();
    lv_slider_set_value(control->brightness_slider, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(control->brightness_label, "%u%%", value);
}

static void open_settings(lv_event_t *event) {
    struct touch_screen_controls *control = lv_event_get_user_data(event);
    refresh_brightness(control);
    lv_obj_remove_flag(control->settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(control->settings_panel);
}

static void close_settings(lv_event_t *event) {
    struct touch_screen_controls *control = lv_event_get_user_data(event);
    lv_obj_add_flag(control->settings_panel, LV_OBJ_FLAG_HIDDEN);
}

static void slider_changed(lv_event_t *event) {
    struct touch_screen_controls *control = lv_event_get_user_data(event);
    uint8_t value = (uint8_t)lv_slider_get_value(control->brightness_slider);
    lv_label_set_text_fmt(control->brightness_label, "%u%%", value);
    (void)prospector_brightness_set_manual(value);
}

static void screen_gesture(lv_event_t *event) {
    struct touch_screen_controls *control = lv_event_get_user_data(event);
    if (prospector_device_screen_is_open() ||
        !lv_obj_has_flag(control->settings_panel, LV_OBJ_FLAG_HIDDEN)) {
        return; /* Keep slider drags inside the brightness page. */
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_TOP:
        prospector_device_screen_open();
        lv_indev_wait_release(indev);
        break;
    case LV_DIR_BOTTOM:
        open_settings(event);
        lv_indev_wait_release(indev);
        break;
    case LV_DIR_LEFT:
        prospector_status_layout_cycle(1);
        lv_indev_wait_release(indev);
        break;
    case LV_DIR_RIGHT:
        prospector_status_layout_cycle(-1);
        lv_indev_wait_release(indev);
        break;
    default:
        break;
    }
}

/* Status widgets reflect ZMK state. Let the root receive navigation gestures
 * even over a roller/container; brightness controls are added after this pass. */
static void make_status_children_passive(lv_obj_t *parent) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        make_status_children_passive(child);
    }
}

void prospector_touch_settings_attach(lv_obj_t *screen) {
    if (controls_count >= PROSPECTOR_LAYOUT_COUNT) {
        return;
    }
    struct touch_screen_controls *control = &shared_control;
    controls_count++;
    make_status_children_passive(screen);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, screen_gesture, LV_EVENT_GESTURE, control);
    if (control->settings_panel) { return; }
    lv_obj_t *settings_panel = control->settings_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(settings_panel, lv_pct(100), lv_pct(100));
    lv_obj_center(settings_panel);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(PSP_CONTROL_BG), 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_panel, 0, LV_PART_MAIN);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(settings_panel);
    style_action_label(title);
    lv_label_set_text(title, "BRIGHTNESS");
    lv_obj_set_pos(title, 16, 10);
    prospector_control_rule(settings_panel);

    lv_obj_t *brightness_label = control->brightness_label = lv_label_create(settings_panel);
    lv_label_set_text_fmt(brightness_label, "%u%%",
                          prospector_brightness_get_manual());
    style_action_label(brightness_label);
    lv_obj_set_style_text_font(brightness_label, &FR_Medium_32, 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 59);

    lv_obj_t *brightness_slider = control->brightness_slider = lv_slider_create(settings_panel);
    lv_obj_set_size(brightness_slider, 232, 12);
    lv_obj_set_pos(brightness_slider, 24, 124);
    lv_obj_set_ext_click_area(brightness_slider, 16);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(PSP_CONTROL_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_slider, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(TOUCH_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(brightness_slider, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(TOUCH_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(brightness_slider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(brightness_slider, 4, LV_PART_KNOB);
    lv_obj_set_style_pad_ver(brightness_slider, 6, LV_PART_KNOB);
    lv_slider_set_range(brightness_slider, 10, 100);
    lv_slider_set_value(brightness_slider, prospector_brightness_get_manual(), LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, slider_changed,
                        LV_EVENT_VALUE_CHANGED, control);
    lv_obj_t *minimum = lv_label_create(settings_panel);
    prospector_control_small(minimum);
    lv_label_set_text(minimum, "10%");
    lv_obj_set_pos(minimum, 20, 151);
    lv_obj_t *maximum = lv_label_create(settings_panel);
    prospector_control_small(maximum);
    lv_label_set_text(maximum, "100%");
    lv_obj_align(maximum, LV_ALIGN_TOP_RIGHT, -20, 151);

    lv_obj_t *close_btn = lv_button_create(settings_panel);
    lv_obj_set_size(close_btn, 94, 38);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    style_action_button(close_btn);
    lv_obj_add_event_cb(close_btn, close_settings, LV_EVENT_CLICKED, control);
    lv_obj_t *close_text = lv_label_create(close_btn);
    lv_label_set_text(close_text, "CLOSE");
    style_action_label(close_text);
    lv_obj_center(close_text);
}
