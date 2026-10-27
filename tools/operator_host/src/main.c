#include <lvgl.h>
#include <prospector_render_state.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zmk/events/caps_word_state_changed.h>
#include <prospector/device_screen.h>

lv_obj_t *zmk_display_status_screen(void);
int operator_widget_modifier_indicator_cb(const zmk_event_t *eh);
static uint32_t ticks;
void host_device_screen_tests(void);
void host_peripheral_set(uint8_t slot, bool connected, uint8_t level);
void host_peripheral_publish(void);
#ifdef PROSPECTOR_HOST_ALL_LAYOUTS
void host_peripheral_screen_tests(void);
#endif

void host_advance_time(uint32_t ms) {
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 5) {
        ticks += 5;
        lv_timer_handler();
    }
}

static void host_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

uint32_t custom_tick_get(void) {
    return ticks;
}

static zmk_mod_flags_t parse_mods(const char *mods) {
    zmk_mod_flags_t flags = 0;
    if (!mods) {
        return flags;
    }

    char buf[128];
    strncpy(buf, mods, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (char *part = strtok(buf, ","); part; part = strtok(NULL, ",")) {
        while (*part == ' ') {
            part++;
        }
        if (strcasecmp(part, "gui") == 0 || strcasecmp(part, "win") == 0 ||
            strcasecmp(part, "cmd") == 0 || strcasecmp(part, "g") == 0) {
            flags |= MOD_LGUI;
        } else if (strcasecmp(part, "alt") == 0 || strcasecmp(part, "a") == 0) {
            flags |= MOD_LALT;
        } else if (strcasecmp(part, "ctrl") == 0 || strcasecmp(part, "control") == 0 ||
                   strcasecmp(part, "c") == 0) {
            flags |= MOD_LCTL;
        } else if (strcasecmp(part, "shift") == 0 || strcasecmp(part, "s") == 0) {
            flags |= MOD_LSFT;
        }
    }
    return flags;
}

static bool parse_bool(const char *value) {
    return value && (strcasecmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
                     strcasecmp(value, "yes") == 0 || strcasecmp(value, "y") == 0 ||
                     strcasecmp(value, "connected") == 0 || strcasecmp(value, "on") == 0);
}

static void apply_batteries(const char *levels, const char *connected) {
    char level_buf[128];
    char conn_buf[128];
    strncpy(level_buf, levels ? levels : "", sizeof(level_buf) - 1);
    level_buf[sizeof(level_buf) - 1] = '\0';
    strncpy(conn_buf, connected ? connected : "", sizeof(conn_buf) - 1);
    conn_buf[sizeof(conn_buf) - 1] = '\0';

    char *conn_save = NULL;
    char *conn_part = strtok_r(conn_buf, ",", &conn_save);
    char *level_save = NULL;
    int index = 0;
    for (char *level_part = strtok_r(level_buf, ",", &level_save); level_part;
         level_part = strtok_r(NULL, ",", &level_save)) {
        bool is_connected = conn_part ? parse_bool(conn_part) : true;
        int level = atoi(level_part);
        host_peripheral_set(index, is_connected, level);
        if (conn_part) {
            conn_part = strtok_r(NULL, ",", &conn_save);
        }
        index++;
    }
    host_peripheral_publish();
}

static const char *arg_value(int *index, int argc, char **argv) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", argv[*index]);
        exit(2);
    }
    *index += 1;
    return argv[*index];
}

int main(int argc, char **argv) {
    const char *output = "operator.raw";
    const char *mods = "shift";
    const char *order = "GACS";
    const char *layer = "Base";
    const char *batteries = "65,11";
    const char *connected = "yes,yes";
    int wpm = 67;
    int active_layer = 0;
    int profile = 1;
    bool caps_word = false;
    bool touch_panel = false;
    bool device_page = false;
    bool flash_page = false;
    bool modes_page = false;
    const char *layout = "operator";
    int input_mode = 0;
    bool device_tests = false;
    bool peripheral_tests = false;
    enum zmk_transport transport = ZMK_TRANSPORT_USB;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0) {
            output = arg_value(&i, argc, argv);
        } else if (strcmp(argv[i], "--wpm") == 0) {
            wpm = atoi(arg_value(&i, argc, argv));
        } else if (strcmp(argv[i], "--mods") == 0) {
            mods = arg_value(&i, argc, argv);
        } else if (strcmp(argv[i], "--modifier-order") == 0) {
            order = arg_value(&i, argc, argv);
        } else if (strcmp(argv[i], "--caps-word") == 0) {
            caps_word = true;
        } else if (strcmp(argv[i], "--layer") == 0) {
            layer = arg_value(&i, argc, argv);
        } else if (strcmp(argv[i], "--active-layer") == 0) {
            active_layer = atoi(arg_value(&i, argc, argv));
        } else if (strcmp(argv[i], "--batteries") == 0) {
            batteries = arg_value(&i, argc, argv);
        } else if (strcmp(argv[i], "--connected") == 0) {
            connected = arg_value(&i, argc, argv);
        } else if (strcmp(argv[i], "--transport") == 0) {
            transport = strcmp(arg_value(&i, argc, argv), "ble") == 0 ? ZMK_TRANSPORT_BLE
                                                                      : ZMK_TRANSPORT_USB;
        } else if (strcmp(argv[i], "--profile") == 0) {
            profile = atoi(arg_value(&i, argc, argv));
        } else if (strcmp(argv[i], "--touch-panel") == 0) {
            touch_panel = true;
        } else if (strcmp(argv[i], "--device-page") == 0) {
            device_page = true;
        } else if (strcmp(argv[i], "--modes-page") == 0) {
            device_page = modes_page = true;
        } else if (strcmp(argv[i], "--flash-page") == 0) {
            device_page = flash_page = true;
        } else if (strcmp(argv[i], "--layout") == 0) {
            layout = arg_value(&i, argc, argv);
        } else if (strcmp(argv[i], "--input-mode") == 0) {
            input_mode = atoi(arg_value(&i, argc, argv));
        } else if (strcmp(argv[i], "--peripheral-tests") == 0) {
            peripheral_tests = true;
        } else if (strcmp(argv[i], "--device-tests") == 0) {
            device_tests = true;
        }
    }

    lv_init();
    lv_tick_set_cb(custom_tick_get);
    lv_display_t *display = lv_display_create(280, 240);
    lv_display_set_flush_cb(display, host_flush);
    static uint8_t draw_buf[280 * 240 * 4];
    lv_display_set_buffers(display, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_DIRECT);

    host_state_set_wpm(wpm);
    host_state_set_mods(parse_mods(mods));
    host_state_set_layer(active_layer, layer);
    host_state_set_endpoint(transport);
    host_state_set_profile((uint8_t)(profile - 1));
    host_modifier_order_set(order);

#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    extern int host_mode;
    host_mode = input_mode;
#endif
#ifdef PROSPECTOR_HOST_ALL_LAYOUTS
    if (peripheral_tests) { host_peripheral_screen_tests(); return 0; }
#endif
    lv_obj_t *screen = zmk_display_status_screen();
#ifdef PROSPECTOR_HOST_ALL_LAYOUTS
    extern lv_obj_t *prospector_screen_classic(void);
    extern lv_obj_t *prospector_screen_field(void);
    lv_obj_t *classic = prospector_screen_classic();
    lv_obj_t *field = prospector_screen_field();
    if (!strcmp(layout, "classic")) { screen = classic; }
    if (!strcmp(layout, "field")) { screen = field; }
#endif
    lv_obj_set_size(screen, 280, 240);
    lv_screen_load(screen);

    if (caps_word) {
        struct zmk_caps_word_state_changed ev = {.active = true};
        operator_widget_modifier_indicator_cb((const zmk_event_t *)&ev);
    }
    apply_batteries(batteries, connected);

    lv_obj_update_layout(screen);

    if (device_tests) {
        host_device_screen_tests();
        return 0;
    }
    if (device_page) {
        prospector_device_screen_open();
        if (flash_page) {
            lv_obj_t *panel = lv_obj_get_child(lv_layer_top(), -1);
            lv_obj_t *info = lv_obj_get_child(panel, 0);
            lv_obj_send_event(lv_obj_get_child(info, 3), LV_EVENT_CLICKED, NULL);
        }
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
        if (modes_page) {
            lv_obj_t *panel = lv_obj_get_child(lv_layer_top(), -1);
            lv_obj_t *info = lv_obj_get_child(panel, 0);
            lv_obj_send_event(lv_obj_get_child(info, 4), LV_EVENT_CLICKED, NULL);
        }
#endif
        lv_obj_update_layout(lv_layer_top());
    }

    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    fprintf(stderr, "LVGL operator heap used: %zu bytes\n",
            memory.total_size - memory.free_size);

    if (touch_panel) {
        lv_obj_t *panel = lv_obj_get_child(screen, -1);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(screen);
    }

    lv_draw_buf_t *snapshot = lv_snapshot_take(device_page ? lv_obj_get_child(lv_layer_top(), -1) : screen,
                                               LV_COLOR_FORMAT_ARGB8888);
    if (!snapshot) {
        fprintf(stderr, "Failed to capture LVGL snapshot\n");
        return 1;
    }

    FILE *fp = fopen(output, "wb");
    if (!fp) {
        perror(output);
        return 1;
    }
    fwrite("LVRAW8888\n", 1, 10, fp);
    fprintf(fp, "%u %u %u\n", snapshot->header.w, snapshot->header.h, snapshot->header.stride);
    fwrite(snapshot->data, 1, snapshot->data_size, fp);
    fclose(fp);
    lv_draw_buf_destroy(snapshot);
    return 0;
}
