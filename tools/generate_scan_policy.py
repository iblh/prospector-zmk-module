#!/usr/bin/env python3
"""Generate one central TU in the build tree; leave the pinned West source untouched."""
import argparse
import hashlib
import json
from pathlib import Path

BASE_SHA256 = '46455685cbaa16e4508a70fdd27ca5301f0288165efd6b343598eb963c9b0f3b'


def function_span(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end = brace + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return start, end


def generate(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASE_SHA256:
        raise ValueError('Unsupported/modified ZMK central.c; requires pinned 4dd95d7 source')
    if source == output:
        raise ValueError('Refusing to overwrite the original ZMK central source')
    text = raw.decode()
    for signature, replacement in [
        ('static int start_scanning(void) {', '/* Connection-aware start defined by the module include above. */'),
        ('static int stop_scanning(void) {', '#include <prospector/split_scan_policy_impl.h>')]:
        a, b = function_span(text, signature)
        text = text[:a] + replacement + text[b:]
    # A failed stop (including an operation in progress) must not strand a
    # reserved CONNECTING slot, otherwise rediscovery could be blocked forever.
    before = '''    int err = stop_scanning();
    if (err < 0) {
        return false;
    }'''
    if text.count(before) != 1:
        raise ValueError('Unsupported connection initiation hook')
    text = text.replace(before, '''    int err = stop_scanning();
    if (err < 0) {
        release_peripheral_slot(slot_idx);
        start_scanning();
        return false;
    }''')
    # This generated TU is added by module CMake before app/ble.c. Equal SYS_INIT
    # priorities would run central first, registering ble_central before
    # bt_enable()->settings_subsys_init() clears the dynamic handler list.
    init = 'SYS_INIT(zmk_split_bt_central_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);'
    if text.count(init) != 1:
        raise ValueError('Unsupported central init hook')
    text = text.replace(init, '''BUILD_ASSERT(CONFIG_ZMK_BLE_INIT_PRIORITY == 50,
             "Scan policy init order requires the pinned BLE init priority 50");
SYS_INIT(zmk_split_bt_central_init, APPLICATION, 51);''')
    hook = 'static int finish_init() {\n'
    if text.count(hook) != 1:
        raise ValueError('Unsupported settings commit hook')
    text = text.replace(hook, '''#if defined(CONFIG_PROSPECTOR_LEFT_SLOT_RECOVERY)
#include <prospector/left_slot_recovery.h>
#endif
static int finish_init() {
#if defined(CONFIG_PROSPECTOR_LEFT_SLOT_RECOVERY)
    if (prospector_left_recovery_start(finish_init)) { return 0; }
#endif
''')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text)
    manifest = dict(zmk_base_sha='4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85',
                    original_sha256=BASE_SHA256,
                    generated_sha256=hashlib.sha256(text.encode()).hexdigest(),
                    central_init_priority=51,
                    generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    output.with_suffix('.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    generate(a.source, a.output)
