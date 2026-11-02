/* SPDX-License-Identifier: MIT
 * Diagnostic CDC commands only. One START runs ON/OFF/OFF/ON for 20s each.
 * No settings writes, disconnect requests or changed BLE connection parameters.
 */
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <prospector/pipeline_profile.h>

LOG_MODULE_REGISTER(scan_ab, LOG_LEVEL_INF);
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static uint32_t run_id, phase;
static int64_t phase_due, state_due;
static bool active;
static char command[48];
static size_t used;
static bool discard;

static void scan_restore(const char *event) {
    active = false; phase = 0;
    int err = pipe_scan_apply(false);
    unsigned int links;
    bool actual = pipe_scan_status(&links);
    LOG_INF("SCANAB run=%u event=%s phase=0 scan=%u links=%u err=%d t_ms=%u",
            run_id, event, actual, links, err, k_uptime_get_32());
}

static bool scan_phase(void) {
    bool off = phase == 2 || phase == 3;
    int err = pipe_scan_apply(off);
    unsigned int links;
    bool actual = pipe_scan_status(&links);
    LOG_INF("SCANAB run=%u event=PHASE phase=%u scan=%u links=%u err=%d t_ms=%u",
            run_id, phase, actual, links, err, k_uptime_get_32());
    if (err || links != 1 || actual == off) { scan_restore("ABORT"); return false; }
    phase_due = k_uptime_get() + 20000;
    return true;
}

static void scan_command(char *line) {
    const char *start = "!P8 START ", *stop = "!P8 STOP ";
    bool is_start = !strncmp(line, start, strlen(start));
    bool is_stop = !strncmp(line, stop, strlen(stop));
    if (!is_start && !is_stop) return;
    char *end;
    char first = line[strlen(is_start ? start : stop)];
    if (first < '1' || first > '9') return;
    errno = 0;
    unsigned long id = strtoul(line + strlen(is_start ? start : stop), &end, 10);
    if (errno || *end || !id || id > UINT32_MAX) return;
    if (is_stop) { if (active && run_id == id) scan_restore("STOP"); return; }
    unsigned int links;
    pipe_scan_status(&links);
    if (active || links != 1) {
        LOG_INF("SCANAB run=%u event=REJECT phase=%u links=%u err=%d", (uint32_t)id, phase, links, -EBUSY);
        return;
    }
    run_id = id; phase = 1; active = true; scan_phase();
}

void pipe_scan_tick(void) {
    if (device_is_ready(console)) {
        unsigned char ch;
        for (unsigned int n = 0; n < 64 && !uart_poll_in(console, &ch); n++) {
            if (ch == '\n' || ch == '\r') {
                command[used] = 0;
                if (!discard) scan_command(command);
                used = 0; discard = false;
            } else if (!discard && used < sizeof(command) - 1) command[used++] = ch;
            else { used = 0; discard = true; }
        }
    }
    unsigned int links;
    bool actual = pipe_scan_status(&links);
    if (active && links != 1) scan_restore("ABORT");
    if (active && k_uptime_get() >= phase_due) {
        if (++phase > 4) scan_restore("END");
        else scan_phase();
    }
    if (k_uptime_get() >= state_due) {
        actual = pipe_scan_status(&links);
        LOG_INF("SCANAB run=%u event=STATE phase=%u scan=%u links=%u err=0 t_ms=%u",
                run_id, phase, actual, links, k_uptime_get_32());
        state_due = k_uptime_get() + 2000;
    }
}
