/* Real LVGL regression: peripherals connect before other layouts are constructed. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <lvgl.h>
#include <prospector/peripheral_state.h>
void host_peripheral_set(uint8_t, bool, uint8_t);
void host_peripheral_publish(void);
void host_advance_time(uint32_t);
lv_obj_t *zmk_display_status_screen(void);
lv_obj_t *prospector_screen_classic(void);
lv_obj_t *prospector_screen_field(void);
static lv_obj_t *find_text(lv_obj_t *obj, const char *text) {
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) { return obj; }
    for (uint32_t i=0; i<lv_obj_get_child_count(obj); i++) {
        lv_obj_t *found=find_text(lv_obj_get_child(obj,i),text); if (found) { return found; }
    }
    return NULL;
}
static void assert_text(lv_obj_t *screen, const char *text) { assert(find_text(screen,text)); }
void host_peripheral_screen_tests(void) {
    assert(CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT == 1);
    lv_obj_t *operator = zmk_display_status_screen();
    lv_screen_load(operator);
    host_peripheral_set(0,true,87);host_peripheral_set(1,true,42);
    host_peripheral_publish();host_advance_time(500);
    assert_text(operator,"42");assert_text(operator,"87");
    // These factories are first visited AFTER both BLE connection events.
    lv_obj_t *classic = prospector_screen_classic();
    lv_obj_t *field = prospector_screen_field();
    host_advance_time(500);
    assert_text(classic,"42");assert_text(classic,"87");assert_text(field,"42/87");
    // Verify the rendered positions, not just a swapped array in a helper test.
    lv_obj_update_layout(operator);
    lv_area_t left_area, right_area;
    lv_obj_get_coords(find_text(operator,"42"), &left_area);
    lv_obj_get_coords(find_text(operator,"87"), &right_area);
    assert(left_area.x1 < right_area.x1);
    lv_obj_update_layout(classic);
    lv_obj_get_coords(find_text(classic,"42"), &left_area);
    lv_obj_get_coords(find_text(classic,"87"), &right_area);
    assert(left_area.x1 < right_area.x1);
    // Coalesced two-slot changes: one display callback must update BOTH halves.
    host_peripheral_set(0,true,61);host_peripheral_set(1,true,73);
    host_peripheral_publish();host_advance_time(500);
    assert_text(operator,"61");assert_text(operator,"73");
    assert_text(classic,"61");assert_text(classic,"73");assert_text(field,"73/61");
    host_peripheral_set(0,false,0);host_peripheral_publish();host_advance_time(500);
    assert_text(field,"73/-");assert_text(operator,"73");
    host_peripheral_set(0,true,48);host_peripheral_publish();host_advance_time(500);
    assert_text(field,"73/48");assert_text(operator,"48");
    lv_obj_t *screens[]={operator,classic,field};
    for (int i=0;i<120;i++) {lv_screen_load(screens[i%3]);host_advance_time(35);}
    assert_text(operator,"73");assert_text(operator,"48");
    assert_text(classic,"73");assert_text(classic,"48");assert_text(field,"73/48");
    puts("PASS real LVGL peripheral regression: late Classic/Field creation, both slots, left/right positions, coalescing, reconnect, 120 screen switches without BLE reconnect");
}
