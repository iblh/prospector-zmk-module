/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_prospector_shortcut
#include <errno.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <prospector/input_mode.h>
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata edits[] = {
    {.display_name = "Copy", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 0},
    {.display_name = "Paste", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1},
    {.display_name = "Cut", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2},
    {.display_name = "Undo", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 3},
    {.display_name = "Redo", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 4},
    {.display_name = "Select All", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 5},
};
static const struct behavior_parameter_metadata_set edit_set = {
    .param1_values = edits, .param1_values_len = ARRAY_SIZE(edits),
};
static const struct behavior_parameter_metadata edit_metadata = {.sets = &edit_set, .sets_len = 1};
#endif
/* Preserve the chosen chord until key-up, even if modifiers change meanwhile. */
static uint32_t held[41];
static const uint32_t chords[][2] = {
    {LG(C), LC(C)}, {LG(V), LC(V)}, {LG(X), LC(X)},
    {LG(Z), LC(Z)}, {LG(LS(Z)), LC(Y)}, {LG(A), LC(A)},
};
static int invoke(struct zmk_behavior_binding *binding,
                  struct zmk_behavior_binding_event event, bool down) {
    if (event.position >= ARRAY_SIZE(held) || binding->param1 >= ARRAY_SIZE(chords)) { return -EINVAL; }
    uint32_t *code = &held[event.position];
    if (down) {
        if (*code) { return -EBUSY; }
        *code = chords[binding->param1][prospector_input_mode_get() != PROSPECTOR_MAC_DV];
    } else if (!*code) { return ZMK_BEHAVIOR_OPAQUE; }
    struct zmk_behavior_binding key = {.behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)), .param1 = *code};
    int rc = zmk_behavior_invoke_binding(&key, event, down);
    if (!down || rc < 0) { *code = 0; }
    return rc;
}
static int pressed(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) { return invoke(b,e,true); }
static int released(struct zmk_behavior_binding *b, struct zmk_behavior_binding_event e) { return invoke(b,e,false); }
static const struct behavior_driver_api api = {
    .binding_pressed = pressed, .binding_released = released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &edit_metadata,
#endif
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
