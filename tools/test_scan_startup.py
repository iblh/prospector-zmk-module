#!/usr/bin/env python3
"""Reproduce startup handler loss and verify ordered actual init/commit hooks."""
from pathlib import Path
import importlib.util,subprocess,tempfile
HERE=Path(__file__).resolve().parent;FW=HERE.parents[1]/'firmware'
spec=importlib.util.spec_from_file_location('g',HERE/'generate_scan_policy.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
with tempfile.TemporaryDirectory() as td:
 root=Path(td);g.generate(FW/'zmk/app/src/split/bluetooth/central.c',root/'central.c');text=(root/'central.c').read_text()
 assert 'SYS_INIT(zmk_split_bt_central_init, APPLICATION, 51);' in text
 def function(signature):
  a,b=g.function_span(text,signature);return text[a:b]
 source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define CONFIG_SETTINGS 1
#define CONFIG_ZMK_BLE_THREAD_PRIORITY 5
#define IS_ENABLED(x) (x)
#define K_THREAD_STACK_SIZEOF(x) sizeof(x)
struct k_work_q{int unused;};static struct k_work_q split_central_split_run_q;
static char split_central_split_run_q_stack[512];static int conn_callbacks;
static void k_work_queue_start(struct k_work_q*q,void*s,int n,int p,void*c){(void)q;(void)s;(void)n;(void)p;(void)c;}
static void bt_conn_cb_register(void*p){(void)p;}
static bool settings_loaded,subsys_ready,is_enabled;static int scan_calls,bt_central;
struct status{bool available;};
static struct status split_central_bt_get_status(void){return (struct status){settings_loaded};}
static int available(void*c,struct status s){(void)c;if(s.available){is_enabled=true;scan_calls++;}return 0;}
static int(*transport_status_cb)(void*,struct status)=available;
struct handler{const char*name;int(*h_commit)(void);};
static struct handler*handlers[8];static int handler_count;
static int settings_register(struct handler*h){handlers[handler_count++]=h;return 0;}
static void settings_subsys_init(void){if(!subsys_ready){handler_count=0;subsys_ready=true;}}
static int ble_commit(void){return 0;}
static struct handler ble_handler={"ble",ble_commit};
static void ble_init(void){settings_subsys_init();settings_register(&ble_handler);}
static int finish_init(void);
static struct handler ble_central_settings_handler={"ble_central",finish_init};
'''+function('static int zmk_split_bt_central_init(void) {')+'\n'+function('static int finish_init() {')+r'''
static void settings_load(void){for(int n=0;n<handler_count;n++)handlers[n]->h_commit();}
static void reset(void){subsys_ready=settings_loaded=is_enabled=false;handler_count=scan_calls=0;}
int main(void){
 reset();zmk_split_bt_central_init();assert(handler_count==1);
 ble_init();assert(handler_count==1);settings_load();
 assert(!settings_loaded&&!is_enabled&&!scan_calls); // T5-09 failure reproduced
 reset();ble_init();zmk_split_bt_central_init();assert(handler_count==2);
 settings_load();assert(settings_loaded&&is_enabled&&scan_calls==1);
 puts("PASS actual central init/commit with Settings lifecycle model: reproduce discarded handler in old order; retain handler and enable scan in BLE-first order");
}
'''
 (root/'test.c').write_text(source)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test')],check=True)
