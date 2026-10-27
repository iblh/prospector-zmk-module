#include <stdint.h>

#include <prospector/brightness.h>
#include <prospector/status_layout.h>

void prospector_status_layout_cycle(int direction) { (void)direction; }

static uint8_t brightness = 65;

uint8_t prospector_brightness_get_manual(void) { return brightness; }

int prospector_brightness_set_manual(uint8_t value) {
    brightness = value;
    return 0;
}
