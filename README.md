# Prospector ZMK Module

This is a [ZMK module](https://zmk.dev/docs/features/modules) that provides custom status screen support for the [Prospector](https://github.com/carrefinho/prospector) display dongle.

![Four status screen layouts for Prospector](docs/images/status-screen-update-hero.png)

> [!IMPORTANT]
> This branch targets the Zephyr 4.1 ZMK stack. The companion firmware manifest pins the tested commits; newer upstream main revisions are not assumed compatible.

## Touch Edition candidate

With `CONFIG_PROSPECTOR_TOUCH_UI=y`, runtime layouts are Classic / Field / Operator;
Radii remains available only to upstream compile-time builds. Touch supplies manual
10–100% brightness, swipe navigation, a device page and confirmed UF2 entry.
Optional MAC-DV / WIN-DV / GAME input modes require the companion dedicated keymap.

Touch mapping, PWM, idle/touch wake and T4-01 device/UF2 UI have user acceptance.
T5-01 typing, BLE pairing, GAME and keyboard wake are pending physical verification.
See the [project guide](https://github.com/iblh/charybdis-wireless-mini-zmk-firmware/blob/feat/touch-no-sensor/docs/prospector-touch/docs/11-input-modes-guide.md)
and [current acceptance record](https://github.com/iblh/charybdis-wireless-mini-zmk-firmware/blob/feat/touch-no-sensor/docs/prospector-touch/docs/06-roadmap-and-acceptance.md).

## Table of Contents

- [Features](#features)
- [Installation](#installation)
- [Status Screens](#status-screens)
- [Usage](#usage)
- [Brightness Controls](#brightness-controls)
- [Idle Timeout (Display + Backlight)](#idle-timeout-display--backlight)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Known Issues](#known-issues)
- [To-Do](#to-do)

## Features

- Four status screen layouts to choose from
- Display blanks and backlight turns off on idle/sleep, then restores on activity
- Active layer display
- Peripheral battery status
- BLE profile and output indicator
- Active modifier display
- Caps word indicator

## Installation

Your ZMK keyboard should be set up with a dongle as central.

Add this module to your `config/west.yml` with these new entries under `remotes` and `projects`:

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: carrefinho                            # <--- add this
      url-base: https://github.com/carrefinho     # <--- and this
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: prospector-zmk-module                 # <--- and these
      remote: carrefinho                          # <---
      revision: feat/new-status-screens           # <---
  self:
    path: config
```

Then add the `prospector_adapter` shield to the dongle in your `build.yaml`:

```yaml
---
include:
  - board: xiao_ble//zmk
    shield: [YOUR KEYBOARD SHIELD]_dongle prospector_adapter
```

For more information on ZMK Modules and building locally, see [the ZMK docs page on modules.](https://zmk.dev/docs/features/modules)

## Status Screens

Classic is used by default. To choose a different screen, add one of the following to your `.conf` file:

```ini
CONFIG_PROSPECTOR_STATUS_SCREEN_RADII=y
CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD=y
CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR=y
```

To capture Operator screen renders for documentation, see
[Operator Status Render](docs/operator-status-render.md). The render script
compiles the Operator LVGL widgets locally and keeps external ZMK/Zephyr/LVGL
dependencies in an ignored local cache.

## Usage

For split keyboards, the peripheral battery widget arranges sub-widgets in pairing order. After flashing the dongle, pair the left side first, then the right side. For more than two peripherals, pair them left to right.

The layer display shows the `display-name` property when available, falling back to the layer index otherwise. To add a `display-name` to a keymap layer:

```dts
keymap {
  compatible = "zmk,keymap";
  base {
    display-name = "Base";           # <--- add this
    bindings = <
      ...
    >;
  }
}
```

## Brightness Controls

Prospector exposes a `&pbl` behavior for display backlight control. Add this include to the top of
your keymap:

```dts
#include <dt-bindings/prospector/brightness.h>
```

Then bind keys like this:

```dts
bindings = <
  &pbl PBL_TOG  /* toggle ambient sensor/manual mode */
  &pbl PBL_INC  /* switch to manual mode and brighten */
  &pbl PBL_DEC  /* switch to manual mode and dim */
>;
```

Additional commands are available:

```dts
&pbl PBL_AUTO      /* force ambient sensor mode */
&pbl PBL_MANUAL    /* force manual mode at the current brightness */
&pbl PBL_SET 75    /* switch to manual mode and set 75% brightness */
```

`PBL_INC` and `PBL_DEC` use `CONFIG_PROSPECTOR_BRIGHTNESS_STEP`, which defaults to `10`.

For `CONFIG_PROSPECTOR_TOUCH_UI=y`, the touch slider and `&pbl` manual commands share one
requested brightness value. A change schedules small backlight steps on a work queue;
the LVGL and key event callbacks do not wait for the fade. Manual changes are saved after
1.2 seconds without another change under `prospector_touch/brightness` in Zephyr Settings.
The saved value is loaded before ZMK initializes the display. The UI refreshes its slider
from that value when the settings panel opens. This is software behavior only: actual
backlight dimming depends on a verified PWM connection to the LCD backlight circuit.
The touch build compiles Classic, Field, and Operator in one image.
Swipe left for the next layout or right for the previous layout; swipe down on the
status screen to open brightness, then tap CLOSE to return. The LIGHT button is
removed, and Operator's full-width modifier row is restored. Status children are
read-only touch targets so the Classic roller cannot consume navigation gestures.
The runtime build starts with only Operator and creates each other screen on first visit. Created screens stay
allocated because ZMK listeners retain widget pointers. Field's animation timer runs
only while Field is visible. Layout choices are saved, but this diagnostic build
intentionally starts in Operator on every boot until safe restoration is proven.
The firmware uses Zephyr's `CONFIG_LV_Z_MEM_POOL_SIZE` for the object heap (49,152
bytes in this diagnostic build), with heap statistics enabled. Runtime heap
capacity, gestures, and keyboard latency still need physical validation.

The user confirmed both the compact four-layout build's layout/swipe tests and the
current three-layout revision's layout cycling and swipe-down brightness entry.
Reboot persistence, wake-touch consumption, and keyboard HID remain separate tests.
Persisted layout IDs remain stable, with the old Radii ID retired.

Swipe up on a status screen to open the shared DEVICE page. It shows firmware
release, build time (America/Los_Angeles), working-source content fingerprint,
current built keymap label, USB readiness and BLE slot K1/K2 connectivity. Slot
numbers do not imply verified physical left/right identity. The current keymap is
QWERTY; the planned MAC-DV/WIN-DV/GAME selector is not implemented yet.

FLASH opens confirmation; a separate continuous two-second hold requests UF2 mode.
Early release, slide-out, CANCEL, timeout and idle cancel it. The reboot worker uses
ZMK's bootmode mapping, checks failure before reboot, and does not clear settings.
This feature passes local UI/service mock tests; actual NICENANO mounting is still
pending device validation. Keep the verified three-layout UF2 for rollback.

The user's clone runs UF2 Bootloader 0.6.0, which accepts application writes only
below `0xAD000`. The first two four-layout images exceeded that boundary and were
not installed completely. Touch builds now generate compact font sources in the
build directory: role-specific glyph subsets, sparse maps, and lossless RLE with
row XOR. Original font sources are retained. Run
`python3 tools/test_compressed_fonts.py /path/to/lvgl` to check the generated maps
and every retained glyph against LVGL's C decoder. If a widget starts using a new
character in a numeric/modifier font, update `tools/compress_lvgl_fonts.py` too.
The workspace build wrapper checks UF2 vectors, family, and bootloader address
limits before reporting success. These checks do not establish hardware acceptance.

## Idle Timeout (Display + Backlight)

`CONFIG_ZMK_IDLE_TIMEOUT` controls when ZMK transitions from `ACTIVE` to `IDLE` after no input activity.

To blank the display at idle, also enable:

```ini
CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=y
```

When that timeout is reached:
- ZMK display blanking turns the screen off.
- Prospector backlight is set to `0`.

`CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE` affects display blanking only. Backlight shutoff is handled by this module on `IDLE`/`SLEEP` activity events.

When activity returns:
- The display is unblanked by ZMK.
- Backlight is restored by this module:
  - fixed/manual mode: the last requested manual brightness (initially `CONFIG_PROSPECTOR_FIXED_BRIGHTNESS`)
  - ambient mode: current sensor-driven brightness

Set the timeout in your dongle `.conf` file (value is in milliseconds):

```ini
# 60 seconds of inactivity before IDLE
CONFIG_ZMK_IDLE_TIMEOUT=60000
CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=y
```

Optional: if you also use ZMK sleep behavior, this module handles both `IDLE` and `SLEEP` by keeping the backlight off until activity resumes.

## Configuration

To customize, add config options to your `.conf` file:
```ini
CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR=n
CONFIG_PROSPECTOR_FIXED_BRIGHTNESS=80
```

### General
| Name | Description | Default |
| ---- | ----------- | ------- |
| `CONFIG_PROSPECTOR_ROTATE_DISPLAY_180` | Rotate the display 180 degrees | n |
| `CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR` | Use ambient light sensor for auto brightness | y |
| `CONFIG_PROSPECTOR_FIXED_BRIGHTNESS` | Fixed display brightness when not using ambient light sensor | 50 (1-100) |
| `CONFIG_PROSPECTOR_BRIGHTNESS_STEP` | Brightness percentage-point step for `&pbl PBL_INC` / `&pbl PBL_DEC` | 10 |
| `CONFIG_PROSPECTOR_LAYER_NAME_UPPERCASE` | Convert layer names to uppercase (Operator and Radii only) | y |
| `CONFIG_ZMK_IDLE_TIMEOUT` | Inactivity time before ZMK enters `IDLE` (backlight turns off; display blanking also requires `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE`) | ZMK default |

> [!NOTE]
> Prospector display backlight is forced to `0` when ZMK enters `IDLE` or `SLEEP`, and restored when activity returns to `ACTIVE`.
> In fixed/manual mode it restores to the last requested manual brightness; in ambient-light mode it restores to the current sensor-driven brightness.

When ambient-light brightness is enabled, Prospector uses a module-owned APDS9960 patch driver
(`CONFIG_PROSPECTOR_APDS9960`). It keeps the normal Zephyr sensor API in use while narrowing the
driver to the display backlight's ALS-only needs. This patch avoids the stock driver's proximity
setup and handles an interrupt edge case where the APDS9960 INT line can already be active before
the fetch path starts waiting.

Do not also enable Zephyr's APDS9960 driver; leave `CONFIG_APDS9960=n` so only one driver binds to
the `avago,apds9960` devicetree node.

### Modifiers
| Name | Description | Default |
| ---- | ----------- | ------- |
| `CONFIG_PROSPECTOR_SHOW_MODIFIERS` | Display modifier key indicators | y |
| `CONFIG_PROSPECTOR_SHOW_INACTIVE_MODIFIERS` | Show inactive modifiers dimmed (Classic and Field only) | y |
| `CONFIG_PROSPECTOR_MODIFIER_ORDER` | Order of modifiers: G=GUI, A=Alt, C=Ctrl, S=Shift | "GACS" |

### Field-specific
| Name | Description | Default |
| ---- | ----------- | ------- |
| `CONFIG_PROSPECTOR_ANIMATION_WPM_REFERENCE` | WPM value at which animation reaches max speed | 70 |
| `CONFIG_PROSPECTOR_ANIMATION_INTENSITY_DECAY_SEC` | Seconds for lines to fade out after typing stops | 30 |
| `CONFIG_PROSPECTOR_ANIMATION_FLOW_DECAY_SEC` | Seconds for line directions and length to settle | 300 |

## Troubleshooting

### RAM overflow error

If you encounter a `region 'RAM' overflowed` error when building, add the following to your `.conf` file to reduce the display buffer size:

```ini
CONFIG_LV_Z_VDB_SIZE=25
```

## Known Issues

- One peripheral may fail to register key presses after connecting to the dongle; reset the affected peripheral to fix. https://github.com/zmkfirmware/zmk/issues/3156
- Operator, Radii: battery display only supports up to three peripherals

## To-Do

- Operator: per-profile BLE status
- Radii: document and improve color theme customization
- OS-specific modifier styles
- Caps lock indication


### Prospector Touch input modes (local T5-01 candidate)

`CONFIG_PROSPECTOR_INPUT_MODES` requires the dedicated firmware-side
`touch_modes.keymap`: complete MAC-DV / WIN-DV / GAME bases (IDs 0/1/2), temporary
NUM/NAV/SYM/FN/SLOW/SCROLL layers (IDs 3–8). Swipe up → DEVICE → MODE selects the
input mode independently of Classic/Field/Operator appearance. The mode worker
waits for all received physical positions to release, 350ms quiet and neutral HID
reports. A GNU ld position-event wrapper observes fresh events before hold-tap
capture, with no upstream ZMK source edits. Do not remove it or replace it with a
late event subscription. The dedicated keymap's longest hold-tap is 280ms.

Typing uses native mod-morph with keep-mods, while GAME letters and primary thumbs
are direct. Mode labels and CMD/OPT versus WIN/ALT update at runtime. Default mode
is MAC-DV; no new settings persistence or BLE host switching is added. Tests live
in `tools/test_input_modes.py` and `tools/test_device_service.py`. The real-source
LVGL host also supports `PROSPECTOR_HOST_INPUT_MODES=ON` and
`PROSPECTOR_HOST_ALL_LAYOUTS=ON` for mode/confirmation input tests and previews.

The local build is a candidate: keyboard-half flashing, BLE pairing, HID shortcuts,
GAME and trackball behavior require physical acceptance. Full workspace keymap,
reproduction and pairing guide: [input-mode guide](https://github.com/iblh/charybdis-wireless-mini-zmk-firmware/blob/feat/touch-no-sensor/docs/prospector-touch/docs/11-input-modes-guide.md).

### T5-02 peripheral status synchronization

Touch layouts hydrate connection/battery widgets from a full shared peripheral
snapshot, including on first visit after BLE has already connected. Display work
coalescing retains both slots. The observer publishes atomic connection state
before notifying widgets and ignores unrelated disconnects.

`CONFIG_PROSPECTOR_LEFT_PERIPHERAL_SLOT` selects which bonded slot appears at the
left of a two-half Touch display. Its default is 0; this project's hardware config
uses 1 because the right half was paired into slot 0. This changes presentation
only, while K1/K2 on the device page remain raw slot numbers. Re-pairing into a
different slot order requires checking this setting again.

`tools/test_peripheral_state.py` compiles the real observer/query against platform
shims. The actual LVGL renderer's `--peripheral-tests` covers late Classic/Field
creation, both-slot refreshes, physical display order, reconnect and 120 screen
switches without simulated BLE reconnect. Hardware acceptance remains separate.
