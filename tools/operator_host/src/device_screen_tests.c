/* Exercises the actual LVGL input dispatcher and UI timers with mock reboot. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include <prospector/device_screen.h>

extern int host_flash_requests, host_flash_request_error, host_flash_async_error;
extern const char *host_usb_state;
void host_advance_time(uint32_t ms);

static lv_point_t point;
static bool pressed;
static lv_indev_t *pointer;

static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point = point;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void touch(int x, int y, bool down) {
    point = (lv_point_t){x, y};
    pressed = down;
    host_advance_time(40);
}

static void tap(int x, int y) {
    touch(x, y, true);
    touch(x, y, false);
}

static bool visible_text(lv_obj_t *obj, const char *text) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0) {
        return true;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        if (visible_text(lv_obj_get_child(obj, i), text)) {
            return true;
        }
    }
    return false;
}

static void open_info(void) {
    prospector_device_screen_open();
    lv_obj_update_layout(lv_layer_top());
    host_advance_time(40);
    assert(visible_text(lv_layer_top(), "DEVICE"));
}

static void confirm_page(void) {
    open_info();
    tap(205, 209);
    assert(visible_text(lv_layer_top(), "ENTER UF2?"));
}

void host_device_screen_tests(void) {
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_pointer);

    open_info();
    touch(205, 209, true);
    host_advance_time(2500); /* Long first contact must not itself confirm. */
    assert(host_flash_requests == 0);
    touch(205, 209, false);
    assert(visible_text(lv_layer_top(), "ENTER UF2?"));
    host_advance_time(2300);
    assert(host_flash_requests == 0);

    tap(195, 209); /* A short second tap also cannot reboot. */
    host_advance_time(2300);
    assert(host_flash_requests == 0);
    touch(195, 209, true);
    host_advance_time(1000);
    touch(195, 209, false);
    host_advance_time(2300);
    assert(host_flash_requests == 0);

    confirm_page();
    touch(195, 209, true);
    host_advance_time(800);
    touch(110, 178, true); /* Leaving and reentering cancels the whole contact. */
    touch(195, 209, true);
    host_advance_time(2300);
    touch(195, 209, false);
    assert(host_flash_requests == 0);
    tap(60, 209);
    assert(visible_text(lv_layer_top(), "DEVICE"));

    confirm_page();
    host_advance_time(15100);
    assert(visible_text(lv_layer_top(), "DEVICE"));
    assert(host_flash_requests == 0);

    confirm_page();
    touch(195, 209, true);
    host_advance_time(800);
    prospector_device_screen_on_idle();
    host_advance_time(2300);
    touch(195, 209, false);
    assert(!prospector_device_screen_is_open());
    assert(host_flash_requests == 0);

    confirm_page();
    touch(195, 209, true);
    host_advance_time(2100);
    assert(host_flash_requests == 1);
    host_advance_time(3000);
    assert(host_flash_requests == 1);
    touch(195, 209, false);
    prospector_device_screen_on_idle();

    host_flash_request_error = -EIO;
    confirm_page();
    touch(195, 209, true);
    host_advance_time(2100);
    touch(195, 209, false);
    assert(host_flash_requests == 2);
    assert(visible_text(lv_layer_top(), "FLASH FAILED (-5)"));
    tap(195, 209);
    assert(host_flash_requests == 2); /* Failed state requires a fresh confirmation. */
    tap(60, 209);
    assert(visible_text(lv_layer_top(), "DEVICE"));

    host_flash_request_error = 0;
    confirm_page();
    touch(195, 209, true);
    host_advance_time(2100);
    touch(195, 209, false);
    host_flash_async_error = -EIO;
    host_advance_time(100);
    assert(host_flash_requests == 3);
    assert(visible_text(lv_layer_top(), "FLASH FAILED (-5)"));
    host_flash_async_error = 0;
    tap(60, 209);
    tap(60, 209);
    assert(!prospector_device_screen_is_open());
    host_usb_state = "SUSPEND";
    open_info();
    lv_obj_t *panel = lv_obj_get_child(lv_layer_top(), -1);
    lv_obj_t *info = lv_obj_get_child(panel, 0);
    lv_obj_t *text = lv_obj_get_child(info, 1);
    assert(lv_obj_get_y(text) + lv_obj_get_height(text) < 190);
    tap(60, 209);
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    extern int host_mode, host_mode_pending, host_mode_requests, host_mode_error;
    open_info();
    tap(130, 136);
    assert(visible_text(lv_layer_top(), "INPUT MODE"));
    assert(visible_text(lv_layer_top(), "ACTIVE: MAC-DV"));
    tap(135, 92); // WIN-DV
    assert(host_mode_requests == 1 && host_mode_pending == 1 && host_mode == 0);
    assert(visible_text(lv_layer_top(), "WAIT: WIN-DV\nRELEASE ALL KEYS"));
    host_mode = 1; host_mode_pending = -1; host_advance_time(40);
    assert(visible_text(lv_layer_top(), "ACTIVE: WIN-DV"));
    tap(135, 132); // GAME
    assert(host_mode_requests == 2 && host_mode_pending == 2);
    host_mode = 2; host_mode_pending = -1; host_advance_time(40);
    assert(visible_text(lv_layer_top(), "ACTIVE: GAME"));
    tap(140, 209); assert(visible_text(lv_layer_top(), "DEVICE"));
    tap(130, 136); host_mode_error = -12;
    tap(135, 52); assert(visible_text(lv_layer_top(), "MODE FAILED (-12)"));
    host_mode_error = 0;
    prospector_device_screen_on_idle(); assert(!prospector_device_screen_is_open());
    puts("PASS mode UI: entry, all selectors, pending-to-active, BACK, errors and idle close");
#endif
    lv_indev_delete(pointer);
    puts("PASS device UI: independent contacts, short/early release, slide-out, cancel, timeout, idle, one-shot request and errors");
}
