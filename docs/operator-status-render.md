# Operator Status Render

Use `tools/render_operator_status.py` to generate Operator status screen images
for docs and visual checks.

The script compiles the real Operator LVGL widget C files, touch settings UI,
and converted LVGL fonts, injects configurable ZMK state through host stubs,
captures the LVGL framebuffer, and writes a PNG. Its touch-enabled render
checks visual placement; it does not emulate the CST816T or verify taps. The
host LVGL config disables all themes to match the firmware. Use `--touch-panel`
to render the settings panel and slider.

The current status render has no LIGHT button. Operator's modifier container uses
the original 230-pixel width at `(25, 8)`. Swipe-down access is verified separately
on hardware; the host renderer checks appearance only.

Use `--device-page` or `--flash-page` for DEVICE and UF2 confirmation previews.
Device metadata/connection state in host previews are mock data; no host preview
can reboot a physical board. The built `operator_renderer --device-tests` exercises
actual LVGL pointer sampling/timers with a mocked reboot service. Run
`python3 tools/test_device_service.py` to test the real service C against host
platform shims, including retained-write failure and no-reboot checks.

## Dependency Cache

External render dependencies stay out of the repo. The script looks for LVGL in
this order:

```text
--lvgl-dir
LVGL_DIR
.render-cache/operator/zmk-workspace/modules/lib/gui/lvgl
nearby local ZMK workspaces
```

If no usable LVGL checkout is found, the script automatically prepares the
ignored cache at `.render-cache/operator/`: it creates a local virtualenv,
installs `west`, initializes a ZMK workspace, and runs `west update`. ZMK,
Zephyr, and LVGL are not committed because `.render-cache/` is ignored.

You can also prepare the cache explicitly:

```sh
python tools/render_operator_status.py --prepare-deps
```

## Interactive Render

```sh
python tools/render_operator_status.py --interactive
```

It prompts for every visible Operator state value:

```text
WPM
active modifiers
caps word state
modifier order
layer display name
layer count / horizontal bars
active layer index
peripheral battery levels
peripheral connection states
USB/BLE output
BLE profile count
selected BLE profile
PNG scale
output path
```

`PNG scale` is an integer output multiplier. The firmware screen is 280x240
pixels. `--scale 3` writes an 840x720 PNG with nearest-neighbor enlargement,
which is easier to inspect in documentation without changing the rendered
screen geometry.

The same state can also be rendered non-interactively:

```sh
python tools/render_operator_status.py \
  --wpm 67 \
  --mods shift \
  --layer Base \
  --layer-count 5 \
  --active-layer 0 \
  --batteries 65,11 \
  --connected yes,yes \
  --transport usb \
  --profile-count 4 \
  --profile 1 \
  --output docs/images/operator.png
```

The script defaults to that scenario, so this shorter command generates the
same render:

```sh
python tools/render_operator_status.py
```

Useful options:

```text
--lvgl-dir path/to/lvgl
--wpm 67
--mods gui,shift
--caps-word
--batteries 65,11
--connected yes,no
--transport usb|ble
--profile-count 4
--profile 1
--layer Base
--layer-count 5
--active-layer 0
--scale 3
--output path/to/render.png
```
