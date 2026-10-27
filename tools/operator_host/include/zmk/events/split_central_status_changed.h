#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <zmk/event_manager.h>
struct zmk_split_central_status_changed { uint8_t slot; bool connected; };
static inline const struct zmk_split_central_status_changed *as_zmk_split_central_status_changed(const zmk_event_t *eh) { assert(eh != NULL); return (const struct zmk_split_central_status_changed *)eh; }
