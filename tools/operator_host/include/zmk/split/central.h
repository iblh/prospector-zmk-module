#pragma once
#include <zephyr/kernel.h>
#include <stdint.h>
int zmk_split_central_get_peripheral_battery_level(uint8_t source, uint8_t *level);
