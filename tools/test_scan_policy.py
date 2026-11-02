#!/usr/bin/env python3
"""Real generated integration and scan C: discovery, reconnect and transient errors."""
import hashlib,importlib.util,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent;MODULE=HERE.parent;FW=MODULE.parent/'firmware'
spec=importlib.util.spec_from_file_location('generator',HERE/'generate_scan_policy.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
def body(p):return '\n'.join(s for s in p.read_text().splitlines() if not s.startswith(('#include','#pragma')))
with tempfile.TemporaryDirectory() as td:
 root=Path(td);source=FW/'zmk/app/src/split/bluetooth/central.c';original=source.read_bytes()
 g.generate(source,root/'generated.c');text=(root/'generated.c').read_text()
 assert source.read_bytes()==original
 assert text.count('#include <prospector/split_scan_policy_impl.h>')==1
 assert 'static int start_scanning(void) {' not in text
 bad=root/'bad.c';bad.write_bytes(original+b'\n')
 try:g.generate(bad,root/'invalid.c')
 except ValueError:pass
 else:raise AssertionError('modified core accepted')
 try:g.generate(source,source)
 except ValueError:pass
 else:raise AssertionError('original overwritten')
 a,b=g.function_span(text,'static bool split_central_eir_found(');eir=text[a:b]
 c=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS 2
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct k_mutex{bool held;};
#define K_MUTEX_DEFINE(x) static struct k_mutex x
static int k_mutex_lock(struct k_mutex*m,int wait){assert(wait==0);if(m->held)return -EBUSY;m->held=true;return 0;}
static void k_mutex_unlock(struct k_mutex*m){m->held=false;}
struct k_work{int unused;};struct k_work_delayable{struct k_work work;void(*fn)(struct k_work*);bool pending;};
#define K_WORK_DELAYABLE_DEFINE(x,f) static struct k_work_delayable x={.fn=f}
static void*zmk_workqueue_lowprio_work_q(void){return (void*)1;}
static int k_work_reschedule_for_queue(void*q,struct k_work_delayable*w,int delay){assert(q==(void*)1&&delay==1000);w->pending=true;return 0;}
static void k_work_cancel_delayable(struct k_work_delayable*w){w->pending=false;}
struct net_buf_simple{int unused;};typedef struct {int slot;}bt_addr_le_t;
struct bt_le_scan_param{unsigned int interval,window,type,options;};
#define BT_LE_SCAN_PASSIVE (&(struct bt_le_scan_param){96,48,0,1})
static unsigned int starts,stops,window,interval;
static int start_error,stop_error;static bool radio;static void(*start_hook)(void);
static int bt_le_scan_start(struct bt_le_scan_param*p,void(*cb)(const bt_addr_le_t*,int8_t,uint8_t,struct net_buf_simple*)){
 assert(p->type==0&&p->options==1&&cb);starts++;
 if(start_error)return start_error;
 if(radio)return -EALREADY;
 radio=true;interval=p->interval;window=p->window;if(start_hook)start_hook();return 0;
}
static int bt_le_scan_stop(void){stops++;if(stop_error)return stop_error;if(!radio)return -EALREADY;radio=false;return 0;}
enum {PERIPHERAL_SLOT_STATE_OPEN,PERIPHERAL_SLOT_STATE_CONNECTING,PERIPHERAL_SLOT_STATE_CONNECTED};
struct peripheral_slot{int state;void*conn;};static struct peripheral_slot peripherals[2];
static bool is_enabled=true,is_scanning;
static int start_scanning(void);
'''+body(MODULE/'include/prospector/split_scan_policy.h')+'\n'+body(MODULE/'include/prospector/split_scan_policy_impl.h')+r'''
static void split_central_device_found(const bt_addr_le_t*a,int8_t r,uint8_t t,struct net_buf_simple*b){(void)a;(void)r;(void)t;(void)b;}
static int reserve_peripheral_slot(const bt_addr_le_t*a){int n=a->slot;if(n<0||n>1||peripherals[n].state)return -ENOMEM;peripherals[n].state=1;return n;}
static void release_peripheral_slot(int n){peripherals[n]=(struct peripheral_slot){0};}
struct bt_le_conn_param{int unused;};
#define CONFIG_ZMK_SPLIT_BLE_PREF_INT 6
#define CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY 30
#define CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT 400
#define BT_CONN_LE_CREATE_CONN NULL
#define BT_HCI_OP_LE_CREATE_CONN 0
#define BT_LE_CONN_PARAM(i,j,l,t) (&(struct bt_le_conn_param){0})
static int create_result;static unsigned creates;
static int bt_conn_le_create(const bt_addr_le_t*a,void*p,struct bt_le_conn_param*c,void**out){(void)a;(void)p;(void)c;creates++;if(create_result)return create_result;*out=(void*)99;return 0;}
'''+eir+r'''
static void disable_in_start(void){is_enabled=false;start_hook=NULL;}
static void retry(void){assert(scan_policy_retry.pending);scan_policy_retry.pending=false;scan_policy_retry.fn(&scan_policy_retry.work);}
int main(void){
 assert(!start_scanning()&&radio&&interval==96&&window==48);unsigned count=starts;
 assert(!start_scanning()&&starts==count); // no redundant restart
 bt_addr_le_t a={0},b={1};assert(!split_central_eir_found(&a));assert(!radio&&creates==1&&peripherals[0].state==1);
 assert(!start_scanning()&&!radio); // no scanning during connection initiation
 peripherals[0].state=2;assert(!start_scanning()&&radio&&interval==320&&window==8);
 count=creates;scan_policy_mutex.held=true;
 assert(!split_central_eir_found(&b));assert(creates==count&&peripherals[1].state==0&&scan_policy_retry.pending);
 scan_policy_mutex.held=false;retry();assert(radio&&interval==320&&!scan_policy_retry.pending);
 assert(!split_central_eir_found(&b)&&!radio&&peripherals[1].state==1);
 peripherals[1].state=2;assert(!start_scanning()&&!radio&&!scan_policy_retry.pending);
 // Two->one restarts background; one->zero must upgrade an already active scan.
 release_peripheral_slot(1);assert(!start_scanning()&&radio&&interval==320);
 release_peripheral_slot(0);assert(!start_scanning()&&radio&&interval==96);
 // Start/stop failures are retried without stranding state or slots.
 stop_error=-EIO;assert(!split_central_eir_found(&a));assert(peripherals[0].state==0&&radio);
 stop_error=0;
 // A failed mode transition retains the old parameters and retries later.
 peripherals[0].state=2;peripherals[0].conn=(void*)99;assert(!start_scanning()&&interval==320);
 release_peripheral_slot(0);stop_error=-EIO;
 assert(start_scanning()==-EIO&&scan_policy_retry.pending&&interval==320);
 stop_error=0;retry();assert(radio&&interval==96&&!scan_policy_retry.pending);
 assert(!stop_scanning());start_error=-EIO;assert(start_scanning()==-EIO&&!is_scanning&&scan_policy_retry.pending);
 start_error=0;retry();assert(is_scanning&&radio&&!scan_policy_retry.pending);
 create_result=-EIO;assert(!split_central_eir_found(&a));assert(!peripherals[0].state&&is_scanning&&radio);create_result=0;
 // Unexpected EALREADY triggers a controlled stop/restart, not false success.
 assert(!stop_scanning());radio=true;assert(start_scanning()==-EALREADY&&scan_policy_current==P_SCAN_UNKNOWN);
 retry();assert(radio&&scan_policy_current==P_SCAN_FAST&&!scan_policy_retry.pending);
 // Disable while another operation owns the lock: queued reconciliation stops it.
 scan_policy_mutex.held=true;is_enabled=false;assert(stop_scanning()==-EAGAIN&&scan_policy_retry.pending);
 scan_policy_mutex.held=false;retry();assert(!radio&&!is_scanning&&!scan_policy_retry.pending);
 is_enabled=true;assert(!start_scanning()&&radio);
 assert(!stop_scanning());start_hook=disable_in_start;
 assert(!start_scanning()&&radio&&scan_policy_retry.pending);
 retry();assert(!radio&&!scan_policy_retry.pending); // state changed during a real API call
 assert(prospector_scan_mode(false,0,2,false)==P_SCAN_OFF);
 assert(prospector_scan_mode(true,2,2,false)==P_SCAN_OFF);
 assert(prospector_scan_mode(true,1,2,true)==P_SCAN_OFF);
 puts("PASS actual scan policy/integration: 0/1/2 halves, pending connect, failed connect/stop cleanup, reconnect upgrades, contention/retry, disable, EALREADY recovery; source hash guard and original preservation");
}
'''
 file=root/'test.c';file.write_text(c)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(file),'-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test')],check=True)
