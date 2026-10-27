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

/* The firmware intentionally has no LVGL theme. Every new control needs its
 * own visible styles; otherwise LVGL defaults to transparent/black on black. */
#define TOUCH_ACCENT 0x299BE8
#define TOUCH_ACCENT_PRESSED 0x1676BB
#define TOUCH_TEXT 0xF0F7FC

static void style_action_button(lv_obj_t *button) {
    lv_obj_set_style_bg_color(button, lv_color_hex(TOUCH_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(TOUCH_ACCENT_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
}

static void style_action_label(lv_obj_t *label) {
    lv_obj_set_style_text_color(label, lv_color_hex(TOUCH_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &FG_Medium_20, LV_PART_MAIN);
}

struct touch_screen_controls {
    lv_obj_t *settings_panel;
    lv_obj_t *brightness_slider;
    lv_obj_t *brightness_label;
};
static struct touch_screen_controls controls[PROSPECTOR_LAYOUT_COUNT];
static unsigned int controls_count;

static void refresh_brightness(struct touch_screen_controls *control) {
    uint8_t value = prospector_brightness_get_manual();
    lv_slider_set_value(control->brightness_slider, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(control->brightness_label, "BRIGHTNESS  %u%%", value);
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
    lv_label_set_text_fmt(control->brightness_label, "BRIGHTNESS  %u%%", value);
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
    struct touch_screen_controls *control = &controls[controls_count++];
    make_status_children_passive(screen);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen, screen_gesture, LV_EVENT_GESTURE, control);
    lv_obj_t *settings_panel = control->settings_panel = lv_obj_create(screen);
    lv_obj_set_size(settings_panel, lv_pct(100), lv_pct(100));
    lv_obj_center(settings_panel);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(0x121A1C), 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_panel, 0, LV_PART_MAIN);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brightness_label = control->brightness_label = lv_label_create(settings_panel);
    lv_label_set_text_fmt(brightness_label, "BRIGHTNESS  %u%%",
                          prospector_brightness_get_manual());
    style_action_label(brightness_label);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *brightness_slider = control->brightness_slider = lv_slider_create(settings_panel);
    lv_obj_set_width(brightness_slider, lv_pct(90));
    lv_obj_set_height(brightness_slider, 18);
    lv_obj_align(brightness_slider, LV_ALIGN_CENTER, 0, -12);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x324653), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_slider, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(TOUCH_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(brightness_slider, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(TOUCH_TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(brightness_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_slider, 6, LV_PART_KNOB);
    lv_slider_set_range(brightness_slider, 10, 100);
    lv_slider_set_value(brightness_slider, prospector_brightness_get_manual(), LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, slider_changed,
                        LV_EVENT_VALUE_CHANGED, control);

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
