/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

enum prospector_scan_mode { P_SCAN_OFF, P_SCAN_FAST, P_SCAN_BACKGROUND, P_SCAN_UNKNOWN };
/* Zephyr uses 0.625ms units: 320=200ms, 8=5ms. */
#define P_SCAN_BG_INTERVAL 320
#define P_SCAN_BG_WINDOW 8
static inline enum prospector_scan_mode
prospector_scan_mode(bool enabled, unsigned int connected, unsigned int total, bool connecting) {
    if (!enabled || connecting || connected >= total) return P_SCAN_OFF;
    return connected ? P_SCAN_BACKGROUND : P_SCAN_FAST;
}
