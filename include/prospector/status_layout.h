/* SPDX-License-Identifier: MIT */
#pragma once

enum prospector_status_layout {
    /* Keep persisted IDs stable; ID 1 was the retired Radii layout. */
    PROSPECTOR_LAYOUT_CLASSIC = 0,
    PROSPECTOR_LAYOUT_FIELD = 2,
    PROSPECTOR_LAYOUT_OPERATOR = 3,
    PROSPECTOR_LAYOUT_SLOT_COUNT = 4,
    PROSPECTOR_LAYOUT_COUNT = 3,
};

/* Call on the LVGL display work queue only. Positive moves forward. */
void prospector_status_layout_cycle(int direction);
