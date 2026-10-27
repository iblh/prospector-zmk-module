#include <modifier_order.h>
#include <prospector/input_mode.h>

#include <ctype.h>
#include <string.h>

static enum modifier_type order[MOD_TYPE_COUNT] = {
    MOD_TYPE_GUI, MOD_TYPE_ALT, MOD_TYPE_CTRL, MOD_TYPE_SHIFT
};

static const char *texts[MOD_TYPE_COUNT] = {
    "GUI", "ALT", "CTRL", "SHIFT"
};

static enum modifier_type char_to_modifier(char ch) {
    switch (toupper((unsigned char)ch)) {
    case 'G':
        return MOD_TYPE_GUI;
    case 'A':
        return MOD_TYPE_ALT;
    case 'C':
        return MOD_TYPE_CTRL;
    case 'S':
        return MOD_TYPE_SHIFT;
    default:
        return MOD_TYPE_COUNT;
    }
}

void host_modifier_order_set(const char *order_str) {
    if (!order_str || strlen(order_str) != MOD_TYPE_COUNT) {
        return;
    }

    bool seen[MOD_TYPE_COUNT] = {false};
    enum modifier_type parsed[MOD_TYPE_COUNT];
    for (int i = 0; i < MOD_TYPE_COUNT; i++) {
        enum modifier_type type = char_to_modifier(order_str[i]);
        if (type >= MOD_TYPE_COUNT || seen[type]) {
            return;
        }
        seen[type] = true;
        parsed[i] = type;
    }

    memcpy(order, parsed, sizeof(order));
}

enum modifier_type modifier_order_get(int position) {
    if (position < 0 || position >= MOD_TYPE_COUNT) {
        return MOD_TYPE_GUI;
    }
    return order[position];
}

bool modifier_order_uses_symbols(void) {
    return false;
}

bool modifier_order_is_windows(void) {
    return false;
}

const char *modifier_order_get_symbol(int position) {
    return modifier_order_get_text(position);
}

const char *modifier_order_get_text(int position) {
#ifdef CONFIG_PROSPECTOR_INPUT_MODES
    static const char *mac[] = {"CMD", "OPT", "CTRL", "SHFT"};
    static const char *win[] = {"WIN", "ALT", "CTRL", "SHFT"};
    return (prospector_input_mode_get() == 0 ? mac : win)[modifier_order_get(position)];
#else
    return texts[modifier_order_get(position)];
#endif
}
