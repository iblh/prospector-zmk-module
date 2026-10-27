/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zmk/activity.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/split_central_status_changed.h>
#include <zmk/usb.h>
#include <prospector/device_service.h>
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
#include <prospector/input_mode.h>
#endif
#include <prospector/device_screen.h>
#include <prospector_build_info.h>

static atomic_t connected_slots;
static atomic_t flash_pending;
static atomic_t flash_error;

void prospector_device_info_get(struct prospector_device_info *info) {
    const char *usb = "NO HOST";
    if (zmk_usb_get_status() == USB_DC_SUSPEND) {
        usb = "SUSPEND";
    } else if (zmk_usb_is_hid_ready()) {
        usb = "READY";
    } else if (zmk_usb_is_powered()) {
        usb = "POWER";
    }
    atomic_val_t connected = atomic_get(&connected_slots);
    *info = (struct prospector_device_info){
        .version = CONFIG_PROSPECTOR_RELEASE_TAG,
        .build_time = PROSPECTOR_BUILD_TIME,
        .source_id = PROSPECTOR_SOURCE_ID,
        #ifdef CONFIG_PROSPECTOR_INPUT_MODES
        .keymap = prospector_input_mode_name(prospector_input_mode_get()),
#else
        .keymap = CONFIG_PROSPECTOR_KEYMAP_LABEL,
#endif
        .usb = usb,
        .connected = {(connected & 1) != 0, (connected & 2) != 0},
    };
}

static void bootloader_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!atomic_cas(&flash_pending, 1, 0)) {
        return;
    }
    if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        atomic_set(&flash_error, -ECANCELED);
        return;
    }
    int rc = bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
    if (rc < 0) {
        atomic_set(&flash_error, rc);
        return; /* Keep the working application if the retained write fails. */
    }
    sys_reboot(SYS_REBOOT_WARM);
}
K_WORK_DELAYABLE_DEFINE(bootloader_work, bootloader_work_handler);

int prospector_device_request_bootloader(void) {
    if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        return -ECANCELED;
    }
    if (!atomic_cas(&flash_pending, 0, 1)) {
        return -EBUSY;
    }
    atomic_set(&flash_error, 0);
    int rc = k_work_reschedule(&bootloader_work, K_MSEC(100));
    if (rc < 0) {
        atomic_set(&flash_pending, 0);
    }
    return rc < 0 ? rc : 0;
}

void prospector_device_cancel_bootloader(void) {
    atomic_set(&flash_pending, 0);
    (void)k_work_cancel_delayable(&bootloader_work);
}

int prospector_device_bootloader_error(void) { return atomic_get(&flash_error); }

static void idle_ui_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    prospector_device_screen_on_idle();
}
K_WORK_DEFINE(idle_ui_work, idle_ui_work_handler);

static int device_activity_listener(const zmk_event_t *event) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(event);
    if (ev && ev->state != ZMK_ACTIVITY_ACTIVE) {
        prospector_device_cancel_bootloader();
        if (zmk_display_is_initialized()) {
            k_work_submit_to_queue(zmk_display_work_q(), &idle_ui_work);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(prospector_device_activity, device_activity_listener);
ZMK_SUBSCRIPTION(prospector_device_activity, zmk_activity_state_changed);

static int device_connection_listener(const zmk_event_t *event) {
    const struct zmk_split_central_status_changed *ev = as_zmk_split_central_status_changed(event);
    if (ev && ev->slot < 2) {
        atomic_val_t bit = BIT(ev->slot);
        if (ev->connected) {
            atomic_or(&connected_slots, bit);
        } else {
            atomic_and(&connected_slots, ~bit);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(prospector_device_connection, device_connection_listener);
ZMK_SUBSCRIPTION(prospector_device_connection, zmk_split_central_status_changed);
