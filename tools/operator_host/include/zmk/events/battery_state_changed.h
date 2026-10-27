#pragma once
#include <assert.h>
#include <stdint.h>
#include <zmk/event_manager.h>
struct zmk_peripheral_battery_state_changed { uint8_t source; uint8_t state_of_charge; };
static inline const struct zmk_peripheral_battery_state_changed *as_zmk_peripheral_battery_state_changed(const zmk_event_t *eh) { assert(eh != NULL); return (const struct zmk_peripheral_battery_state_changed *)eh; }
