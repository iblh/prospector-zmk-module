/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

/* Hold split transport unavailable until the boot-only operation has finished. */
bool prospector_left_recovery_start(int (*resume)(void));
const char *prospector_left_recovery_status(void);
