/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_prospector_input_mode
#include <drivers/behavior.h>
#include <prospector/input_mode.h>
static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    return prospector_input_mode_request(binding->param1);
}
static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding); ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api api = {
    .binding_pressed = pressed, .binding_released = released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
