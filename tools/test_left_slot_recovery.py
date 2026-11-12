#!/usr/bin/env python3
"""Run actual recovery C with persistent Settings/bond models and fault injection."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source='\n'.join(line for line in (ROOT/'src/left_slot_recovery.c').read_text().splitlines() if not line.startswith('#include'))
preamble=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT 1
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS 2
#define CONFIG_BT_ID_MAX 1
#define BUILD_ASSERT(c,m) _Static_assert(c,m)
#define BT_ID_DEFAULT 0
#define BT_ADDR_LE_RANDOM 1
#define SYS_REBOOT_COLD 0
struct bt_addr_t{uint8_t val[6];};
typedef struct{uint8_t type;struct bt_addr_t a;}bt_addr_le_t;
static const bt_addr_le_t any={0},none={.type=255,.a={{255,255,255,255,255,255}}};
#define BT_ADDR_LE_ANY (&any)
#define BT_ADDR_LE_NONE (&none)
static int bt_addr_le_cmp(const bt_addr_le_t*a,const bt_addr_le_t*b){return memcmp(a,b,sizeof(*a));}
typedef int atomic_t;
static int atomic_get(atomic_t*p){return *p;}
static void atomic_set(atomic_t*p,int n){*p=n;}
static bool atomic_cas(atomic_t*p,int a,int b){if(*p!=a)return false;*p=b;return true;}
struct k_work{void(*fn)(struct k_work*);};
struct k_work_delayable{struct k_work work;};
#define K_WORK_DELAYABLE_DEFINE(n,f) static struct k_work_delayable n={{f}}
#define K_SECONDS(n) ((n)*1000)
static struct k_work*job;
static int dummyq;
static void*zmk_workqueue_lowprio_work_q(void){return &dummyq;}
static int submit_error;
static int k_work_reschedule_for_queue(void*q,struct k_work_delayable*w,int ms){(void)q;assert(ms==2000);if(submit_error)return -EIO;job=&w->work;return 1;}
static int rebooted,resumed,unpaired,deleted,saved,mutation_count,fail_save_at;
static void sys_reboot(int x){(void)x;rebooted++;}
typedef int(*settings_read_cb)(void*,void*,size_t);
struct entry{char name[80];uint8_t bytes[512];size_t len;};
static struct entry db[20];static int entries;
static const char*fail_save;static const char*fail_delete;static const char*fail_read;
static bool hide_key_delete,unpair_fail,corrupt_right;
static bt_addr_le_t right={{0}},old_left={{0}},new_left={{0}};
struct bt_bond_info{bt_addr_le_t addr;};static bt_addr_le_t bonds[8];static int bond_count;
static struct entry*get(const char*n){for(int i=0;i<entries;i++)if(!strcmp(db[i].name,n))return &db[i];return NULL;}
static void put(const char*n,const void*p,size_t len){struct entry*e=get(n);if(!e){assert(entries<20);e=&db[entries++];strcpy(e->name,n);}e->len=len;memcpy(e->bytes,p,len);}
static int readcb(void*a,void*b,size_t n){struct entry*e=a;if(fail_read&&!strcmp(fail_read,e->name))return -EIO;memcpy(b,e->bytes,n);return (int)n;}
static int settings_load_subtree_direct(const char*n,int(*cb)(const char*,size_t,settings_read_cb,void*,void*),void*p){struct entry*e=get(n);if(e&&e->len)cb("",e->len,readcb,e,p);return 0; /* actual loader hides callback errors */}
static int settings_save_one(const char*n,const void*p,size_t z){saved++;if(saved==fail_save_at||(fail_save&&!strcmp(n,fail_save)))return -EIO;put(n,p,z);mutation_count++;return 0;}
static int settings_delete(const char*n){deleted++;assert(!strcmp(n,"ble/peripheral_addresses/1"));if(fail_delete&&!strcmp(n,fail_delete))return -EIO;struct entry*e=get(n);if(e)e->len=0;mutation_count++;return 0;}
static void bt_foreach_bond(uint8_t id,void(*f)(const struct bt_bond_info*,void*),void*p){assert(id==0);for(int i=0;i<bond_count;i++){struct bt_bond_info b={bonds[i]};f(&b,p);}}
static void path_for(char*p,const bt_addr_le_t*a){snprintf(p,24,"bt/keys/%02x%02x%02x%02x%02x%02x%u",a->a.val[5],a->a.val[4],a->a.val[3],a->a.val[2],a->a.val[1],a->a.val[0],a->type);}
static int bt_unpair(uint8_t id,const bt_addr_le_t*a){assert(id==0&&a&&bt_addr_le_cmp(a,BT_ADDR_LE_ANY)&&bt_addr_le_cmp(a,&right));assert(!bt_addr_le_cmp(a,&old_left));unpaired++;if(unpair_fail)return -EIO;for(int i=0;i<bond_count;i++)if(!bt_addr_le_cmp(&bonds[i],a)){bonds[i]=bonds[--bond_count];break;}char p[24];path_for(p,a);if(!hide_key_delete&&get(p))get(p)->len=0;if(corrupt_right){path_for(p,&right);get(p)->bytes[0]^=1;}mutation_count++;return 0;}
'''
tests=r'''
static uint8_t original_key[32];static char rkey[24],lkey[24];
static int resume(void){return finish_init();}
static void reset_runtime(void){settings_loaded=false;started=finished=status=0;job=NULL;resume_transport=NULL;rebooted=resumed=unpaired=deleted=saved=mutation_count=0;}
static void setup(bool left_present){
 memset(db,0,sizeof(db));entries=bond_count=0;fail_save=fail_delete=fail_read=NULL;submit_error=fail_save_at=0;
 hide_key_delete=unpair_fail=corrupt_right=false;reset_runtime();
 right=(bt_addr_le_t){.type=1,.a={{1,2,3,4,5,6}}};old_left=(bt_addr_le_t){.type=1,.a={{11,12,13,14,15,16}}};new_left=(bt_addr_le_t){.type=1,.a={{21,22,23,24,25,26}}};
 memset(original_key,0x9a,sizeof(original_key));path_for(rkey,&right);path_for(lkey,&old_left);
 put(LR_RIGHT_PATH,&right,sizeof(right));put(rkey,original_key,sizeof(original_key));bonds[bond_count++]=right;
 if(left_present){put(LR_LEFT_PATH,&old_left,sizeof(old_left));put(lkey,original_key,sizeof(original_key));bonds[bond_count++]=old_left;}
}
static void run(void){assert(!finish_init());assert(!resumed&&!settings_loaded);assert(!finish_init());assert(job);struct k_work*w=job;job=NULL;w->fn(w);}
static void right_kept(void){assert(get(LR_RIGHT_PATH)->len==sizeof(right));assert(!memcmp(get(LR_RIGHT_PATH)->bytes,&right,sizeof(right)));assert(get(rkey)->len==sizeof(original_key));assert(!memcmp(get(rkey)->bytes,original_key,sizeof(original_key)));assert(has_bond(&right));}
static void add_new_left(void){put(LR_LEFT_PATH,&new_left,sizeof(new_left));char p[24];path_for(p,&new_left);put(p,original_key,sizeof(original_key));bonds[bond_count++]=new_left;}
int main(void){
#if CONFIG_PROSPECTOR_LEFT_RECOVERY_GENERATION == 2
 setup(true);struct repair_record prior={.magic=LR_RECORD_MAGIC,.state=LR_COMPLETE,.right=right,.old_left=new_left};
 put("prospector_left_repair/v1",&prior,sizeof(prior));run();
 assert(rebooted==1&&unpaired==1&&deleted==1);right_kept();
 assert(!memcmp(get("prospector_left_repair/v1")->bytes,&prior,sizeof(prior)));
 add_new_left();reset_runtime();run();assert(resumed==1&&!rebooted&&!unpaired&&!deleted&&!saved);right_kept();
 assert(has_bond(&new_left));
 puts("PASS second replacement: v1 completion preserved, v2 runs once, new left survives reboot");
#endif
 setup(true);for(int type=0;type<2;type++){char a[40],b[40];bt_addr_le_t addr=old_left;addr.type=type;key_path(a,sizeof(a),&addr);bt_settings_encode_key(b,sizeof(b),"keys",&addr,NULL);assert(!strcmp(a,b));}
 setup(true);run();assert(rebooted==1&&!resumed&&unpaired==1&&deleted==1);right_kept();assert(!get(LR_LEFT_PATH)->len);assert(!get(lkey)->len);
 add_new_left();reset_runtime();run();assert(resumed==1&&!rebooted&&!unpaired&&!deleted&&!saved);assert(has_bond(&new_left));right_kept();assert(!memcmp(get(LR_LEFT_PATH)->bytes,&new_left,sizeof(new_left)));
 setup(false);run();assert(resumed==1&&!unpaired&&!deleted);right_kept();add_new_left();reset_runtime();run();assert(resumed==1&&!unpaired&&!deleted);assert(has_bond(&new_left));
 setup(true);fail_save=LR_RECORD_PATH;run();assert(!resumed&&!rebooted&&!unpaired&&!deleted);right_kept();
 setup(false);fail_save=LR_RECORD_PATH;run();assert(!resumed&&!rebooted&&!unpaired&&!deleted);right_kept();
 setup(true);fail_read=LR_RIGHT_PATH;run();assert(!saved&&!unpaired&&!resumed);right_kept();
 setup(true);fail_read=rkey;run();assert(!saved&&!unpaired&&!resumed);right_kept();
 setup(true);bond_count=0;run();assert(!saved&&!unpaired&&!resumed);
 setup(true);put(LR_LEFT_PATH,&right,sizeof(right));run();assert(!saved&&!unpaired&&!resumed);right_kept();
 setup(true);unpair_fail=true;run();assert(!resumed&&!rebooted&&!deleted);right_kept();unpair_fail=false;reset_runtime();run();assert(rebooted==1);right_kept();
 setup(true);hide_key_delete=true;run();assert(!resumed&&!rebooted&&!deleted);right_kept();hide_key_delete=false;reset_runtime();run();assert(rebooted==1);right_kept();
 setup(true);fail_delete=LR_LEFT_PATH;run();assert(!resumed&&!rebooted);right_kept();fail_delete=NULL;reset_runtime();run();assert(rebooted==1);right_kept();
 setup(true);get(lkey)->len=0;bond_count=1;run();assert(rebooted==1);right_kept();
 setup(true);struct repair_record rec={.magic=LR_RECORD_MAGIC,.state=LR_PENDING,.right=right,.old_left=old_left};put(LR_RECORD_PATH,&rec,sizeof(rec));get(LR_LEFT_PATH)->len=0;run();assert(rebooted==1);right_kept();
 setup(true);rec=(struct repair_record){.magic=LR_RECORD_MAGIC,.state=LR_PENDING,.right=right,.old_left=old_left};put(LR_RECORD_PATH,&rec,sizeof(rec));add_new_left();run();assert(!resumed&&!unpaired&&!deleted&&!saved);right_kept();assert(has_bond(&new_left));
 setup(true);rec.magic=0;put(LR_RECORD_PATH,&rec,sizeof(rec));run();assert(!resumed&&!unpaired&&!deleted&&!saved);right_kept();
 setup(true);put(LR_LEFT_PATH,&old_left,1);run();assert(!resumed&&!unpaired&&!deleted&&!saved);right_kept();
 setup(true);corrupt_right=true;run();assert(!resumed&&!rebooted&&atomic_get(&status)==LR_VERIFY_ERROR);
 setup(true);fail_save_at=2;run();assert(!resumed&&!rebooted&&!get(LR_LEFT_PATH)->len);right_kept();fail_save_at=0;reset_runtime();run();assert(rebooted==1);right_kept();
 setup(true);submit_error=1;assert(prospector_left_recovery_start(resume));assert(!unpaired&&!deleted&&!finished&&!job);
 puts("PASS actual recovery: exact left identity only; slot0/key preserved; gate, no-op one-shot, new-left reboot, pending retry, partial failure, corrupt/changed data and read/write/unpair error guards");
}
'''
# Avoid GCC/Clang warnings in the simple fake address initialization above.
preamble=preamble.replace('right={{0}},old_left={{0}},new_left={{0}}','right={0},old_left={0},new_left={0}')
with tempfile.TemporaryDirectory() as td:
 p=Path(td)
 spec=importlib.util.spec_from_file_location('generator',ROOT/'tools/generate_scan_policy.py')
 generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
 generator.generate(ROOT.parent/'firmware/zmk/app/src/split/bluetooth/central.c',p/'central.c')
 text=(p/'central.c').read_text();a,b=generator.function_span(text,'static int finish_init() {')
 integration=r'''
#define CONFIG_PROSPECTOR_LEFT_SLOT_RECOVERY 1
static bool settings_loaded;static int bt_central;
struct test_status{bool available;};
static struct test_status split_central_bt_get_status(void){return (struct test_status){settings_loaded};}
static int notify(void*t,struct test_status s){(void)t;assert(s.available);resumed++;return 0;}
static int(*transport_status_cb)(void*,struct test_status)=notify;
'''+text[a:b]+'\n'
 pinned=(ROOT.parent/'firmware/zephyr/subsys/bluetooth/host/settings.c').read_text()
 a,b=generator.function_span(pinned,'void bt_settings_encode_key(')
 integration+='#define snprintk snprintf\n#define LOG_DBG(...) ((void)0)\n'+pinned[a:b]+'\n'
 (p/'test.c').write_text(preamble+source+integration+tests)
 for generation in (1,2):
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',f'-DCONFIG_PROSPECTOR_LEFT_RECOVERY_GENERATION={generation}',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
