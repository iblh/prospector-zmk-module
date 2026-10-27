#!/usr/bin/env python3
"""Compile the real device service against deterministic, non-hardware shims."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
platform = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define ARG_UNUSED(x) (void)(x)
#define K_MSEC(x) (x)
#define BIT(x) (1 << (x))
typedef int atomic_t;
typedef int atomic_val_t;
static int atomic_get(atomic_t *p) {return *p;}
static int atomic_set(atomic_t *p,int v) {int old=*p;*p=v;return old;}
static bool atomic_cas(atomic_t *p,int old,int next) {if(*p!=old)return false;*p=next;return true;}
static void atomic_or(atomic_t *p,int v) {*p|=v;}
static void atomic_and(atomic_t *p,int v) {*p&=v;}
struct k_work {void (*handler)(struct k_work *);};
struct k_work_delayable {struct k_work work;};
struct k_work_q {int unused;};
#define K_WORK_DEFINE(n,cb) struct k_work n={cb}
#define K_WORK_DELAYABLE_DEFINE(n,cb) struct k_work_delayable n={{cb}}
int k_work_reschedule(struct k_work_delayable *,int);
int k_work_cancel_delayable(struct k_work_delayable *);
int k_work_submit_to_queue(struct k_work_q *,struct k_work *);
enum zmk_activity_state {ZMK_ACTIVITY_ACTIVE,ZMK_ACTIVITY_IDLE,ZMK_ACTIVITY_SLEEP};
enum zmk_activity_state zmk_activity_get_state(void);
enum {BOOT_MODE_TYPE_BOOTLOADER=1,SYS_REBOOT_WARM=0};
int bootmode_set(int mode);
void sys_reboot(int type);
bool zmk_display_is_initialized(void);
struct k_work_q *zmk_display_work_q(void);
typedef void zmk_event_t;
#define ZMK_LISTENER(n,cb)
#define ZMK_SUBSCRIPTION(n,t)
#define ZMK_EV_EVENT_BUBBLE 0
struct zmk_activity_state_changed {enum zmk_activity_state state;};
struct zmk_split_central_status_changed {uint8_t slot;bool connected;};
static const struct zmk_activity_state_changed *as_zmk_activity_state_changed(const void *p) {return p;}
static const struct zmk_split_central_status_changed *as_zmk_split_central_status_changed(const void *p) {return p;}
enum {USB_DC_DISCONNECTED,USB_DC_SUSPEND,USB_DC_CONFIGURED};
int zmk_usb_get_status(void);
bool zmk_usb_is_hid_ready(void);
bool zmk_usb_is_powered(void);
#define CONFIG_PROSPECTOR_RELEASE_TAG "TEST"
#define CONFIG_PROSPECTOR_KEYMAP_LABEL "QWERTY"
'''
test = r'''
static int test_mode;
static const char *mode_names[] = {"MAC-DV", "WIN-DV", "GAME"};
int prospector_input_mode_get(void) { return test_mode; }
const char *prospector_input_mode_name(int mode) { return mode_names[mode]; }
static struct k_work_delayable *pending;
static int schedule_error,boot_error,boot_calls,reboots,ui_cancels;
static bool ready=true,powered=true,initialized=true;
static int usb_status=USB_DC_CONFIGURED;
static enum zmk_activity_state activity=ZMK_ACTIVITY_ACTIVE;
int k_work_reschedule(struct k_work_delayable *w,int delay) {(void)delay;if(schedule_error)return schedule_error;pending=w;return 1;}
int k_work_cancel_delayable(struct k_work_delayable *w) {if(pending==w)pending=NULL;return 0;}
int k_work_submit_to_queue(struct k_work_q *q,struct k_work *w) {(void)q;w->handler(w);return 0;}
enum zmk_activity_state zmk_activity_get_state(void) {return activity;}
int bootmode_set(int mode) {assert(mode==BOOT_MODE_TYPE_BOOTLOADER);boot_calls++;return boot_error;}
void sys_reboot(int type) {assert(type==SYS_REBOOT_WARM);reboots++;}
bool zmk_display_is_initialized(void) {return initialized;}
struct k_work_q *zmk_display_work_q(void) {return NULL;}
void prospector_device_screen_on_idle(void) {ui_cancels++;}
int zmk_usb_get_status(void) {return usb_status;}
bool zmk_usb_is_hid_ready(void) {return ready;}
bool zmk_usb_is_powered(void) {return powered;}
static void run_work(void) {if(pending){struct k_work_delayable *w=pending;pending=NULL;w->work.handler(&w->work);}}
int main(void) {
    assert(prospector_device_request_bootloader()==0);
    assert(boot_calls==0 && reboots==0);
    assert(prospector_device_request_bootloader()==-EBUSY);
    run_work();assert(boot_calls==1 && reboots==1);
    boot_error=-EIO;
    assert(prospector_device_request_bootloader()==0);
    run_work();assert(boot_calls==2 && reboots==1);
    assert(prospector_device_bootloader_error()==-EIO);
    boot_error=0;
    assert(prospector_device_request_bootloader()==0);
    prospector_device_cancel_bootloader();run_work();assert(boot_calls==2);
    assert(prospector_device_request_bootloader()==0);
    activity=ZMK_ACTIVITY_IDLE;run_work();assert(boot_calls==2);
    assert(prospector_device_bootloader_error()==-ECANCELED);
    assert(prospector_device_request_bootloader()==-ECANCELED);
    activity=ZMK_ACTIVITY_ACTIVE;schedule_error=-ENOMEM;
    assert(prospector_device_request_bootloader()==-ENOMEM);
    schedule_error=0;assert(prospector_device_request_bootloader()==0);
    struct zmk_activity_state_changed idle={ZMK_ACTIVITY_IDLE};
    device_activity_listener(&idle);run_work();assert(ui_cancels==1 && boot_calls==2);
    initialized=false;device_activity_listener(&idle);assert(ui_cancels==1);
    struct prospector_device_info info;
    prospector_device_info_get(&info);assert(!strcmp(info.usb,"READY"));
    ready=false;prospector_device_info_get(&info);assert(!strcmp(info.usb,"POWER"));
    usb_status=USB_DC_SUSPEND;prospector_device_info_get(&info);assert(!strcmp(info.usb,"SUSPEND"));
    usb_status=USB_DC_DISCONNECTED;powered=false;
    prospector_device_info_get(&info);assert(!strcmp(info.usb,"NO HOST"));
    struct zmk_split_central_status_changed link={0,true};
    device_connection_listener(&link);prospector_device_info_get(&info);
    assert(info.connected[0] && !info.connected[1]);
    link.slot=255;device_connection_listener(&link);link.slot=0;link.connected=false;
    device_connection_listener(&link);prospector_device_info_get(&info);assert(!info.connected[0]);
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    for (test_mode = 0; test_mode < 3; test_mode++) {
        prospector_device_info_get(&info);assert(!strcmp(info.keymap, mode_names[test_mode]));
    }
#else
    assert(!strcmp(info.keymap,"QWERTY"));
#endif
    puts("PASS device service: deferred reboot, bootmode failure, cancellation, idle, schedule errors, USB and slot state");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'platform.h').write_text(platform)
    for name in ['zephyr/kernel.h', 'zephyr/retention/bootmode.h', 'zephyr/sys/atomic.h',
                 'zephyr/sys/reboot.h', 'zmk/activity.h', 'zmk/display.h', 'zmk/event_manager.h',
                 'zmk/events/activity_state_changed.h', 'zmk/events/split_central_status_changed.h', 'zmk/usb.h']:
        header = path / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "platform.h"\n')
    (path / 'prospector_build_info.h').write_text('#define PROSPECTOR_BUILD_TIME "TEST"\n#define PROSPECTOR_SOURCE_ID "TEST"\n')
    source = root / 'boards/shields/prospector_adapter/src/device_service.c'
    (path / 'test.c').write_text(f'#include "{source}"\n' + test)
    for flags in [[], ['-DCONFIG_PROSPECTOR_INPUT_MODES=1']]:
        subprocess.run(['cc', *flags, '-I', str(path), '-I', str(root / 'include'),
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
