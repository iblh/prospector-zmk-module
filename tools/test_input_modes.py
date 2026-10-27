#!/usr/bin/env python3
"""Exercise production mode worker, native ZMK mod-morph, shortcuts and keymap contracts.
No hardware is mocked as passing. Requires local firmware/zmk sources and a C compiler.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
workspace = root.parent
keymap = (workspace / 'firmware/config/keymaps/touch_modes.keymap').read_text()
q = 'ESC Q W E R T Y U I O P BSPC TAB A S D F G H J K L SEMI ENTER LSHFT Z X C V B N M COMMA DOT FSLH RSHFT'.split()
d = 'ESC SQT COMMA DOT P Y F G C R L BSPC TAB A O E U I D H T N S ENTER LSHFT SEMI Q J K X B M W V Z RSHFT'.split()
# Check complete layout matrix and shortcut masks, including punctuation.
for mode in ('mac', 'win'):
    for i in range(36):
        if q[i] == d[i]: continue
        node = re.search(rf'{mode}_{i}:.*?\{{(.*?)\}};', keymap, re.S).group(1)
        assert f'bindings = <&kp {d[i]}>, <&kp {q[i]}>;' in node
        masks = re.findall(r'(?:mods|keep-mods) = <(.*?)>;', node)
        assert len(masks) == 2 and masks[0] == masks[1]
        assert 'MOD_RCTL' in masks[0] and 'MOD_LCTL' in masks[0]
        assert ('MOD_RGUI' in masks[0]) == (mode == 'mac')
assert re.findall(r'display-name = "(.*?)"',keymap) == ['MAC-DV','WIN-DV','GAME','NUM','NAV','SYM','FN','SLOW','SCROLL']
for label,n in [('MAC',0),('WIN',1),('GAME',2),('NUM',3),('NAV',4),('SYM',5),('FN',6),('SLOW',7),('SCROLL',8)]:
    assert f'#define {label} {n}' in keymap
for name in ('MAC-DV', 'WIN-DV', 'GAME', 'NUM', 'NAV', 'SYM', 'FN', 'SLOW', 'SCROLL'):
    bindings = re.search(rf'display-name = "{name}"; bindings = <(.*?)>;', keymap, re.S).group(1)
    assert bindings.count('&') == 41, name
    if name == 'GAME':
        assert re.findall(r'&kp (\w+)', bindings)[:36] == q
        assert not any(tag in bindings for tag in ('hrm', 'thumb', '&lt', '&mt', '&td'))
assert max(map(int,re.findall(r'tapping-term-ms = <(\d+)>',keymap))) < 350
hrm_mods = {13:('LGUI','LCTRL'),14:('LALT','LALT'),15:('LSHFT','LSHFT'),16:('LCTRL','LGUI'),19:('RCTRL','RGUI'),20:('RSHFT','RSHFT'),21:('RALT','RALT'),22:('RGUI','RCTRL')}
for n,mode in enumerate(('mac','win')):
    text=re.search(r'display-name = "'+('MAC-DV' if n==0 else 'WIN-DV')+r'"; bindings = <(.*?)>;',keymap,re.S).group(1)
    entries=[' '.join(e.split()) for e in re.findall(r'&[^&]+',text)]
    for i in range(36):
        if i in hrm_mods:
            assert entries[i] == f'&{mode}_hrm_{i} {hrm_mods[i][n]} '+(d[i] if d[i]==q[i] else '0')
            node=re.search(rf'{mode}_hrm_{i}:.*?\{{(.*?)\}};',keymap,re.S).group(1)
            assert ('bindings = <&kp>, <'+('&kp' if d[i]==q[i] else f'&{mode}_{i}')+'>;') in node
        else:
            assert entries[i] == ('&kp '+d[i] if d[i]==q[i] else f'&{mode}_{i}')
assert '&scroller { layers = <SCROLL>; };' in keymap
assert '&slow_pointer { layers = <SLOW>; };' in keymap
assert 'combos.dtsi' not in keymap and 'macros.dtsi' not in keymap

platform = r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ARG_UNUSED(x) (void)(x)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define K_MSEC(x) (x)
#define ATOMIC_INIT(x) (x)
typedef int atomic_t;
static int atomic_get(atomic_t *p) {return *p;}
static int atomic_set(atomic_t *p,int v) {int old=*p;*p=v;return old;}
static bool atomic_cas(atomic_t *p,int old,int next) {if(*p!=old)return false;*p=next;return true;}
struct k_work {void (*handler)(struct k_work *);};
struct k_work_delayable {struct k_work work;};
#define K_WORK_DELAYABLE_DEFINE(n,cb) struct k_work_delayable n={{cb}}
int k_work_reschedule(struct k_work_delayable *,int);
int64_t k_uptime_get(void);
struct zmk_position_state_changed {uint8_t source;uint32_t position;bool state;int64_t timestamp;};
struct zmk_hid_keyboard_report {struct zmk_hid_keyboard_report_body {uint8_t modifiers;uint8_t keys[32];} body;};
struct zmk_hid_consumer_report {struct zmk_hid_consumer_report_body {uint16_t keys[6];} body;};
struct zmk_hid_mouse_report {struct {uint8_t buttons;} body;};
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void);
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void);
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void);
typedef uint8_t zmk_mod_flags_t;
zmk_mod_flags_t zmk_hid_get_explicit_mods(void);
int zmk_keymap_layer_to(uint8_t,bool);
struct device {const void *config;void *data;};
struct zmk_behavior_binding {const char *behavior_dev;uint32_t param1,param2;};
struct zmk_behavior_binding_event {uint32_t position;int64_t timestamp;};
struct behavior_driver_api {
 int (*binding_pressed)(struct zmk_behavior_binding *,struct zmk_behavior_binding_event);
 int (*binding_released)(struct zmk_behavior_binding *,struct zmk_behavior_binding_event);
 int locality;
};
#define BEHAVIOR_LOCALITY_CENTRAL 0
#define ZMK_BEHAVIOR_OPAQUE 0
#define BEHAVIOR_DT_INST_DEFINE(...)
#define DEVICE_DT_NAME(n) "kp"
#define DT_NODELABEL(n) n
#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...)
#define DT_HAS_COMPAT_STATUS_OKAY(n) 1
#define DT_INST_FOREACH_STATUS_OKAY(n)
#define IS_ENABLED(x) 0
const struct device *zmk_behavior_get_binding(const char *name);
int zmk_behavior_invoke_binding(struct zmk_behavior_binding *,struct zmk_behavior_binding_event,bool);
int zmk_hid_masked_modifiers_set(zmk_mod_flags_t);
int zmk_hid_masked_modifiers_clear(void);
'''
worker_test = r'''
static int64_t now;
static int schedule_error,layer=-1,switches,raw_calls;
static bool scheduled;
static struct zmk_hid_keyboard_report keyboard;
static struct zmk_hid_consumer_report consumer;
static struct zmk_hid_mouse_report mouse;
static uint8_t mods;
int64_t k_uptime_get(void) {return now;}
int k_work_reschedule(struct k_work_delayable *w,int delay) {
    assert(w==&mode_work && delay==350);if(schedule_error)return schedule_error;scheduled=true;return 1;
}
struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void) {return &keyboard;}
struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void) {return &consumer;}
struct zmk_hid_mouse_report *zmk_hid_get_mouse_report(void) {return &mouse;}
zmk_mod_flags_t zmk_hid_get_explicit_mods(void) {return mods;}
int zmk_keymap_layer_to(uint8_t n,bool locking) {assert(!locking);layer=n;switches++;assert(prospector_input_mode_get()==n);return 0;}
int __real_raise_zmk_position_state_changed(struct zmk_position_state_changed e) {(void)e;raw_calls++;return 0;}
static void pos(int p,bool down) {
    __wrap_raise_zmk_position_state_changed((struct zmk_position_state_changed){.position=p,.state=down});
}
static void tick(int ms) {now+=ms;if(scheduled){scheduled=false;switch_mode(NULL);}}
int main(void) {
    assert(prospector_input_mode_get()==0 && prospector_input_mode_pending()==-1);
    assert(prospector_input_mode_request(3)==-EINVAL);
    pos(36,true);pos(14,true); // FN + GAME selector, both still physically held
    assert(prospector_input_mode_request(2)==0);tick(1000);assert(switches==0);
    pos(14,false);tick(1000);assert(switches==0);
    pos(36,false);tick(349);assert(switches==0);tick(1);assert(layer==2 && switches==1);
    assert(raw_calls==4); // original handler is always called exactly once
    pos(13,true); // pending hold-tap, no HID key/modifier yet
    assert(prospector_input_mode_request(1)==0);tick(500);assert(switches==1);
    pos(13,false);keyboard.body.keys[5]=1;tick(350);assert(switches==1);
    keyboard.body.keys[5]=0;consumer.body.keys[0]=0x123;tick(350);assert(switches==1);
    consumer.body.keys[0]=0;mouse.body.buttons=1;tick(350);assert(switches==1);
    mouse.body.buttons=0;mods=8;tick(350);assert(switches==1);
    mods=0;keyboard.body.modifiers=2;tick(350);assert(switches==1);
    keyboard.body.modifiers=0;tick(350);assert(layer==1 && switches==2);
    assert(prospector_input_mode_request(0)==0);
    assert(prospector_input_mode_request(1)==0);tick(500);assert(switches==2); // cancel pending by selecting current
    assert(prospector_input_mode_pending()==-1);
    assert(prospector_input_mode_request(0)==0);assert(prospector_input_mode_request(2)==0);
    tick(350);assert(layer==2 && switches==3); // newest request wins
    schedule_error=-ENOMEM;assert(prospector_input_mode_request(0)==-ENOMEM);
    assert(prospector_input_mode_pending()==-1);schedule_error=0;
    char label[16];prospector_input_mode_format_layer(6,label,sizeof(label));assert(!strcmp(label,"G/FN"));
    prospector_input_mode_format_layer(1,label,sizeof(label));assert(!strcmp(label,"WIN-DV"));
    puts("PASS mode worker: raw held keys incl unresolved hold-tap, quiet interval, all HID reports, cancel/replace, errors, labels");
}
'''
morph_test = r'''
static uint8_t mods;
static uint32_t emitted;
static bool emitted_down;
static struct behavior_mod_morph_data data;
static struct behavior_mod_morph_config cfg;
static struct device morph = {.config=&cfg,.data=&data};
const struct device *zmk_behavior_get_binding(const char *s) {(void)s;return &morph;}
zmk_mod_flags_t zmk_hid_get_explicit_mods(void) {return mods;}
int zmk_hid_masked_modifiers_set(zmk_mod_flags_t n) {assert(n==0);return 0;}
int zmk_hid_masked_modifiers_clear(void) {return 0;}
int zmk_behavior_invoke_binding(struct zmk_behavior_binding *b,struct zmk_behavior_binding_event e,bool down) {
    (void)e;emitted=b->param1;emitted_down=down;return 0;
}
static const uint32_t normal[] = {NORMAL_KEYS};
static const uint32_t qwerty[] = {QWERTY_KEYS};
int main(void) {
    struct zmk_behavior_binding b={.behavior_dev="morph"};struct zmk_behavior_binding_event e={0};
    for (int mac=0;mac<=1;mac++) for(size_t i=0;i<ARRAY_SIZE(normal);i++) {
        cfg.normal_binding.param1=normal[i];cfg.morph_binding.param1=qwerty[i];
        cfg.mods=mac ? (MOD_LGUI|MOD_RGUI|MOD_LCTL|MOD_RCTL) : (MOD_LCTL|MOD_RCTL);
        cfg.masked_mods=0; // generated DT mods & ~keep-mods
        for(int early=0;early<2;early++) for(int combo=0;combo<256;combo++) {
            mods=combo;uint32_t expected=mods&cfg.mods?qwerty[i]:normal[i];
            assert(on_mod_morph_binding_pressed(&b,e)==0 && emitted_down && emitted==expected);
            assert(on_mod_morph_binding_pressed(&b,e)==-ENOTSUP); // duplicate press cannot overwrite branch
            if(early)mods=0; // release modifier first: must still release the key chosen at key-down
            assert(on_mod_morph_binding_released(&b,e)==0 && !emitted_down && emitted==expected);
            assert(on_mod_morph_binding_released(&b,e)==-ENOTSUP);
        }
    }
    puts("PASS native ZMK mod-morph: all 36 physical positions x 256 modifier masks x both OS profiles x both release orders");
}
'''.replace('NORMAL_KEYS', ','.join(d)).replace('QWERTY_KEYS', ','.join(q))
shortcut_test = r'''
static int mode;
static uint32_t emitted;
static bool emitted_down;
int prospector_input_mode_get(void) {return mode;}
int zmk_behavior_invoke_binding(struct zmk_behavior_binding *b,struct zmk_behavior_binding_event e,bool down) {
    (void)e;emitted=b->param1;emitted_down=down;return 0;
}
int main(void) {
    struct zmk_behavior_binding b={0};struct zmk_behavior_binding_event e={.position=27};
    for(mode=0;mode<3;mode++) for(int action=0;action<6;action++) {
        b.param1=action;uint32_t expected=chords[action][mode!=0];
        assert(pressed(&b,e)==0 && emitted_down && emitted==expected);
        assert(pressed(&b,e)==-EBUSY);
        assert(released(&b,e)==0 && !emitted_down && emitted==expected);
    }
    b.param1=0;mode=0;assert(pressed(&b,e)==0);mode=1;assert(released(&b,e)==0 && emitted==LG(C));
    b.param1=6;assert(pressed(&b,e)==-EINVAL);b.param1=0;e.position=41;assert(pressed(&b,e)==-EINVAL);
    puts("PASS editing shortcut behavior: Mac/Windows/GAME chords, captured key-up, duplicate and invalid inputs");
}
'''
headers = ['zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/device.h','zephyr/logging/log.h','drivers/behavior.h',
 'zmk/event_manager.h','zmk/events/position_state_changed.h','zmk/hid.h','zmk/keymap.h','zmk/behavior.h',
 'zmk/matrix.h','zmk/endpoints.h','zmk/events/keycode_state_changed.h','zmk/events/modifiers_state_changed.h']
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp);(path/'platform.h').write_text(platform)
    for name in headers:
        h=path/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('#include "platform.h"\n')
    for name,source,test in [('worker',root/'src/input_mode.c',worker_test),
        ('morph',workspace/'firmware/zmk/app/src/behaviors/behavior_mod_morph.c',morph_test),
        ('shortcut',root/'src/behaviors/behavior_shortcut.c',shortcut_test)]:
        (path/'test.c').write_text(f'#include <errno.h>\n#include <dt-bindings/zmk/keys.h>\n#include "{source}"\n'+test)
        subprocess.run(['cc','-std=c11','-I',str(path),'-I',str(root/'include'),'-I',str(workspace/'firmware/zmk/app/include'),str(path/'test.c'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
print('PASS keymap contracts: nine complete 41-key layers, full Dvorak/QWERTY maps, keep-mods, GAME direct bindings, pointing layer IDs')
