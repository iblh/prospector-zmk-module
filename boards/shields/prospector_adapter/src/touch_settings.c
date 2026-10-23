/* Prospector Touch settings UI: adds a small SET button and on-screen slider.
 * This deliberately does not replace ZMK's existing status widgets.
 * Target: Zephyr 4.1, LVGL 9, 280Zo Prospector module.
 * Experimental integration code: requires actual Zephyr build + device validation.
 */
#include <lvgl.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <prospector/brightness.h>
#include <prospector/touch_settings.h>

#define SETTINGS_PREFIX "prospector_touch"
#define SETTINGS_BRIGHTNESS SETTINGS_PREFIX "/brightness"

static uint8_t brightness_value = 65;
static lv_obj_t *settings_panel;
static lv_obj_t *brightness_slider;
static lv_obj_t *brightness_label;

static int prospector_settings_set(const char *name, size_t len,
                                   settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(name, "brightness") != 0 || len != sizeof(brightness_value)) {
        return -ENOENT;
    }
    uint8_t incoming = 0;
    int ret = read_cb(cb_arg, &incoming, sizeof(incoming));
    if (ret < 0) return ret;
    if (ret != sizeof(incoming)) return -EINVAL;
    if (incoming >= 1 && incoming <= 100) brightness_value = incoming;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(prospector_touch, SETTINGS_PREFIX, NULL,
                               prospector_settings_set, NULL, NULL);

static void open_settings(lv_event_t *event) {
    LV_UNUSED(event);
    lv_obj_remove_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settings_panel);
}

static void swipe_event(lv_event_t *event) {
    LV_UNUSED(event);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        lv_obj_remove_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_panel);
    } else if (dir == LV_DIR_RIGHT) {
        lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void close_settings(lv_event_t *event) {
    LV_UNUSED(event);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
}

static void slider_changed(lv_event_t *event) {
    LV_UNUSED(event);
    brightness_value = (uint8_t)lv_slider_get_value(brightness_slider);
    lv_label_set_text_fmt(brightness_label, "Brightness  %u%%", brightness_value);
    /* Existing module fades backlight. For production, move this to k_work
     * to prevent visible LVGL stalling during large changes. */
    prospector_brightness_set_manual(brightness_value);
}

static void slider_released(lv_event_t *event) {
    LV_UNUSED(event);
    /* Write once per drag release, not once per slider movement. */
    settings_save_one(SETTINGS_BRIGHTNESS, &brightness_value,
                      sizeof(brightness_value));
}

void prospector_touch_settings_attach(lv_obj_t *screen) {
    /* Assumes ZMK has already loaded Zephyr settings. */
    (void)prospector_brightness_set_manual(brightness_value);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, swipe_event, LV_EVENT_GESTURE, NULL);

    lv_obj_t *open_btn = lv_button_create(screen);
    lv_obj_set_size(open_btn, 43, 27);
    lv_obj_align(open_btn, LV_ALIGN_TOP_RIGHT, -3, 2);
    lv_obj_add_event_cb(open_btn, open_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *open_text = lv_label_create(open_btn);
    lv_label_set_text(open_text, "SET");
    lv_obj_center(open_text);

    settings_panel = lv_obj_create(screen);
    lv_obj_set_size(settings_panel, lv_pct(100), lv_pct(100));
    lv_obj_center(settings_panel);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(0x121A1C), 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLLABLE);

    brightness_label = lv_label_create(settings_panel);
    lv_label_set_text_fmt(brightness_label, "Brightness  %u%%", brightness_value);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 28);

    brightness_slider = lv_slider_create(settings_panel);
    lv_obj_set_width(brightness_slider, lv_pct(90));
    lv_obj_align(brightness_slider, LV_ALIGN_CENTER, 0, -12);
    lv_slider_set_range(brightness_slider, 1, 100);
    lv_slider_set_value(brightness_slider, brightness_value, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, slider_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(brightness_slider, slider_released,
                        LV_EVENT_RELEASED, NULL);

    lv_obj_t *close_btn = lv_button_create(settings_panel);
    lv_obj_set_size(close_btn, 94, 38);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(close_btn, close_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_text = lv_label_create(close_btn);
    lv_label_set_text(close_text, "CLOSE");
    lv_obj_center(close_text);
}
