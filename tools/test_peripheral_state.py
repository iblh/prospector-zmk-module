#!/usr/bin/env python3
"""Compile real connection observer + full snapshot query with deterministic BT/core shims."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
platform=r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#define CONFIG_ZMK_LOG_LEVEL 0
#define CONFIG_ZMK_BLE_INIT_PRIORITY 0
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING 1
#define ZMK_SPLIT_BLE_PERIPHERAL_COUNT 2
#define IS_ENABLED(x) x
#define LOG_MODULE_DECLARE(...)
#define LOG_DBG(...)
#define LOG_ERR(...)
#define SYS_INIT(...)
#define BT_ADDR_LE_STR_LEN 32
#define BT_CONN_ROLE_CENTRAL 0
#define BT_CONN_ROLE_PERIPHERAL 1
#define APPLICATION 0
typedef int atomic_t;
static bool atomic_test_bit(atomic_t *p,unsigned bit) {return (*p&(1<<bit))!=0;}
static void atomic_set_bit(atomic_t *p,unsigned bit) {*p|=1<<bit;}
static void atomic_clear_bit(atomic_t *p,unsigned bit) {*p&=~(1<<bit);}
struct bt_addr_le_t {int slot;};
struct bt_conn {struct bt_addr_le_t addr;int role;};
struct phy {int rx_phy,le_unused;};
struct bt_conn_info {int role;struct {int interval,latency;struct phy *phy;} le;};
struct bt_conn_cb {void (*connected)(struct bt_conn *,uint8_t);void (*disconnected)(struct bt_conn *,uint8_t);};
static const struct bt_addr_le_t *bt_conn_get_dst(struct bt_conn *c) {return &c->addr;}
static int bt_conn_get_info(struct bt_conn *c,struct bt_conn_info *i) {
 static struct phy p;*i=(struct bt_conn_info){.role=c->role,.le={.phy=&p}};return 0;
}
static void bt_addr_le_to_str(const struct bt_addr_le_t *a,char *s,size_t n) {(void)a;(void)s;(void)n;}
static void bt_conn_cb_register(struct bt_conn_cb *c) {(void)c;}
static int bt_conn_get_security(struct bt_conn *c) {(void)c;return 0;}
static int zmk_ble_put_peripheral_addr(const struct bt_addr_le_t *a) {return a->slot;}
struct zmk_split_central_status_changed {uint8_t slot;bool connected;};
int raise_zmk_split_central_status_changed(struct zmk_split_central_status_changed);
int zmk_split_central_get_peripheral_battery_level(uint8_t,uint8_t *);
'''
test=r'''
static uint8_t batteries[]={91,37};
static int events;
int zmk_split_central_get_peripheral_battery_level(uint8_t slot,uint8_t *level) {
 if(slot>=2)return -EINVAL;*level=batteries[slot];return 0;
}
int raise_zmk_split_central_status_changed(struct zmk_split_central_status_changed ev) {
 assert(ev.slot<2);events++;
 // An actual widget queries while the event is being raised: state must be committed first.
 assert(prospector_split_peripheral_connected(ev.slot)==ev.connected);
 return 0;
}
int main(void) {
 struct prospector_peripheral_snapshot state;
 prospector_peripheral_snapshot_get(&state);assert(!state.connected[0]&&!state.connected[1]);
 struct bt_conn right={.addr={0},.role=BT_CONN_ROLE_CENTRAL};
 struct bt_conn left={.addr={1},.role=BT_CONN_ROLE_CENTRAL};
 split_central_connected(&right,0);split_central_connected(&left,0);
 assert(events==2);
 prospector_peripheral_snapshot_get(&state);
 const int l=CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT;
 assert(state.connected[0]&&state.connected[1]);
 assert(state.battery[0]==batteries[l]&&state.battery[1]==batteries[1-l]);
 // A newly initialized layout must get BOTH slots without a new event.
 struct prospector_peripheral_snapshot late;
 prospector_peripheral_snapshot_get(&late);assert(!memcmp(&state,&late,sizeof(state)));
 // Several updates can coalesce into one display refresh without losing either half.
 batteries[0]=66;batteries[1]=72;prospector_peripheral_snapshot_get(&state);
 assert(state.battery[0]==batteries[l]&&state.battery[1]==batteries[1-l]);
 split_central_disconnected(&right,19);prospector_peripheral_snapshot_get(&state);
 assert(state.connected[0]==(l==1)&&state.connected[1]==(l==0));
 assert(prospector_split_peripheral_connected(1)&&!prospector_split_peripheral_connected(0));
 struct bt_conn unrelated={.addr={0},.role=BT_CONN_ROLE_CENTRAL};
 int before=events;split_central_disconnected(&unrelated,19);assert(events==before);
 struct bt_conn host={.addr={0},.role=BT_CONN_ROLE_PERIPHERAL};
 split_central_connected(&host,0);assert(events==before);
 split_central_connected(&right,0);assert(prospector_split_peripheral_connected(0));
 assert(!prospector_split_peripheral_connected(255));
 for(int i=0;i<1000;i++) {prospector_peripheral_snapshot_get(&late);assert(late.connected[0]&&late.connected[1]);}
 puts("PASS peripheral state: late layout hydration, whole-state coalescing, both display orders, commit-before-notify, reconnect and unrelated links");
}
'''
headers=['zephyr/types.h','zephyr/init.h','zephyr/sys/atomic.h','zephyr/bluetooth/bluetooth.h','zephyr/bluetooth/conn.h','zephyr/bluetooth/hci.h','zephyr/sys/byteorder.h','zephyr/logging/log.h','zmk/ble.h','zmk/split/central.h','zmk/events/split_central_status_changed.h']
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'platform.h').write_text(platform)
 for name in headers:
  h=p/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('#include "platform.h"\n')
 source=f'#include "{root}/src/split/bluetooth/central_status_changed_observer.c"\n#include "{root}/src/peripheral_state.c"\n'+test
 (p/'test.c').write_text(source)
 for left in [0,1]:
  subprocess.run(['cc','-std=c11',f'-DCONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT={left}','-I',str(p),'-I',str(root/'include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
