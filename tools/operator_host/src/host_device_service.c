#include <prospector/input_mode.h>
#include <stdio.h>
/* Host-only mocks: these never reboot or change real hardware. */
#include <prospector/device_service.h>

int host_flash_requests;
int host_flash_request_error;
int host_flash_async_error;
const char *host_usb_state = "READY";

void prospector_device_info_get(struct prospector_device_info *info) {
    *info = (struct prospector_device_info){
        .version = "T5-01",
        .build_time = "10-24 20:30",
        .source_id = "ABC123",
        #ifdef CONFIG_PROSPECTOR_INPUT_MODES
        .keymap = prospector_input_mode_name(prospector_input_mode_get()),
#else
        .keymap = "QWERTY",
#endif
        .usb = host_usb_state,
        .connected = {false, false},
    };
}

int prospector_device_request_bootloader(void) {
    host_flash_requests++;
    return host_flash_request_error;
}
void prospector_device_cancel_bootloader(void) {}
int prospector_device_bootloader_error(void) { return host_flash_async_error; }

#ifdef CONFIG_PROSPECTOR_INPUT_MODES
int host_mode = 0, host_mode_pending = -1;
int host_mode_requests, host_mode_error;
int prospector_input_mode_get(void) { return host_mode; }
int prospector_input_mode_pending(void) { return host_mode_pending; }
const char *prospector_input_mode_name(int mode) {
    static const char *names[] = {"MAC-DV", "WIN-DV", "GAME"};
    return mode >= 0 && mode < 3 ? names[mode] : "UNKNOWN";
}
int prospector_input_mode_request(int mode) {
    host_mode_requests++;
    if (!host_mode_error) { host_mode_pending = mode == host_mode ? -1 : mode; }
    return host_mode_error;
}
void prospector_input_mode_format_layer(uint8_t layer, char *buf, size_t size) {
    if (layer < 3) { snprintf(buf,size,"%s",prospector_input_mode_name(layer)); }
    else { snprintf(buf,size,"%c/FN","MWG"[host_mode]); }
}
#endif
