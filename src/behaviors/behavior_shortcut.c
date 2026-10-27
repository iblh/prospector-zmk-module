/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_prospector_shortcut
#include <errno.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <prospector/input_mode.h>
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
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
