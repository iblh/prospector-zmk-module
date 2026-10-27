#!/usr/bin/env python3
"""Render Prospector Operator status screenshots for documentation and review.

The renderer compiles and runs the actual Operator LVGL widget code with small
host-side ZMK stubs, then captures the LVGL framebuffer. External ZMK, Zephyr,
and LVGL dependencies are discovered locally or downloaded into the ignored
.render-cache/operator directory when needed.
"""

from __future__ import annotations

import argparse
import struct
import os
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CACHE_DIR = REPO_ROOT / ".render-cache" / "operator"
ZMK_REPO = "https://github.com/zmkfirmware/zmk.git"
HOST_SOURCE_DIR = REPO_ROOT / "tools" / "operator_host"


def parse_csv_ints(value: str) -> list[int]:
    return [int(part.strip()) for part in value.split(",") if part.strip()]


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def prepare_deps(cache_dir: Path) -> Path:
    """Prepare an ignored dependency cache for the LVGL renderer."""
    cache_dir = cache_dir.resolve()
    venv_dir = cache_dir / "venv"
    workspace_dir = cache_dir / "zmk-workspace"
    west_bin = venv_dir / "bin" / "west"
    python_bin = venv_dir / "bin" / "python"

    cache_dir.mkdir(parents=True, exist_ok=True)

    if not python_bin.exists():
        run([sys.executable, "-m", "venv", str(venv_dir)])

    if not west_bin.exists():
        run([str(python_bin), "-m", "pip", "install", "--upgrade", "pip", "west"])

    if not (workspace_dir / ".west").exists():
        workspace_dir.mkdir(parents=True, exist_ok=True)
        run([str(west_bin), "init", "-m", ZMK_REPO, str(workspace_dir)])

    run([str(west_bin), "update"], cwd=workspace_dir)

    lvgl_dir = workspace_dir / "modules" / "lib" / "gui" / "lvgl"
    zmk_dir = workspace_dir / "zmk"
    if not lvgl_dir.exists() or not zmk_dir.exists():
        raise RuntimeError("west update completed, but expected ZMK/LVGL paths were not created")

    print()
    print("Dependency cache prepared:")
    print(f"  ZMK:  {zmk_dir}")
    print(f"  LVGL: {lvgl_dir}")
    print()
    print("These files are under .render-cache/ and are ignored by git.")
    return lvgl_dir


def find_lvgl_dir(cache_dir: Path, explicit: Path | None = None) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit)
    if "LVGL_DIR" in os.environ:
        candidates.append(Path(os.environ["LVGL_DIR"]))
    candidates.extend(
        [
            cache_dir / "zmk-workspace" / "modules" / "lib" / "gui" / "lvgl",
            REPO_ROOT.parent / "zmk-workspace" / "modules" / "lib" / "gui" / "lvgl",
            REPO_ROOT.parent / "charybdis-wireless-mini-zmk-firmware" / "modules" / "lib" / "gui" / "lvgl",
        ]
    )

    for candidate in candidates:
        if (candidate / "lvgl.h").exists() and (candidate / "CMakeLists.txt").exists():
            return candidate.resolve()
    return None


def raw_to_png(raw_path: Path, png_path: Path, scale: int) -> None:
    with raw_path.open("rb") as fp:
        magic = fp.readline().decode("ascii").strip()
        if magic != "LVRAW8888":
            raise RuntimeError(f"Unexpected renderer output format: {magic}")
        width_s, height_s, stride_s = fp.readline().decode("ascii").split()
        width = int(width_s)
        height = int(height_s)
        stride = int(stride_s)
        data = fp.read()

    pixels = bytearray(width * height * 3)
    for y in range(height):
        row = data[y * stride : y * stride + width * 4]
        for x in range(width):
            b, g, r, _a = row[x * 4 : x * 4 + 4]
            offset = (y * width + x) * 3
            pixels[offset : offset + 3] = bytes((r, g, b))

    if scale > 1:
        scaled_width = width * scale
        scaled_height = height * scale
        scaled = bytearray(scaled_width * scaled_height * 3)
        for y in range(height):
            src = pixels[y * width * 3 : (y + 1) * width * 3]
            scaled_row = bytearray()
            for x in range(width):
                px = src[x * 3 : x * 3 + 3]
                scaled_row.extend(px * scale)
            for repeat in range(scale):
                dest_y = y * scale + repeat
                scaled[dest_y * scaled_width * 3 : (dest_y + 1) * scaled_width * 3] = scaled_row
        width = scaled_width
        height = scaled_height
        pixels = scaled

    png_path.parent.mkdir(parents=True, exist_ok=True)
    write_png_rgb(png_path, width, height, pixels)


def write_png_rgb(path: Path, width: int, height: int, pixels: bytes | bytearray) -> None:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    scanlines = bytearray()
    row_width = width * 3
    for y in range(height):
        scanlines.append(0)
        scanlines.extend(pixels[y * row_width : (y + 1) * row_width])

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
    png.extend(chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9)))
    png.extend(chunk(b"IEND", b""))
    path.write_bytes(png)


def render_operator(args: argparse.Namespace, parser: argparse.ArgumentParser) -> None:
    if shutil.which("cmake") is None:
        parser.error("cmake is required to render the Operator screen")

    lvgl_dir = find_lvgl_dir(args.cache_dir, args.lvgl_dir)
    if lvgl_dir is None:
        if shutil.which("git") is None:
            parser.error("git is required to prepare the dependency cache")
        lvgl_dir = prepare_deps(args.cache_dir)

    battery_count = len(parse_csv_ints(args.batteries))
    build_dir = (
        args.cache_dir
        / "build"
        / f"operator-l{args.layer_count}-p{args.profile_count}-b{battery_count}"
    )
    build_dir.mkdir(parents=True, exist_ok=True)

    run(
        [
            "cmake",
            "-S",
            str(HOST_SOURCE_DIR),
            "-B",
            str(build_dir),
            f"-DPROSPECTOR_ROOT={REPO_ROOT}",
            f"-DLVGL_DIR={lvgl_dir}",
            f"-DOPERATOR_LAYER_COUNT={args.layer_count}",
            f"-DOPERATOR_PROFILE_COUNT={args.profile_count}",
            f"-DOPERATOR_PERIPHERAL_COUNT={battery_count}",
        ]
    )
    run(["cmake", "--build", str(build_dir), "--target", "operator_renderer"])

    with tempfile.TemporaryDirectory() as tmp:
        raw_path = Path(tmp) / "operator.raw"
        command = [
            str(build_dir / "operator_renderer"),
            "--output",
            str(raw_path),
            "--wpm",
            str(args.wpm),
            "--mods",
            args.mods,
            "--modifier-order",
            args.modifier_order,
            "--layer",
            args.layer,
            "--active-layer",
            str(args.active_layer),
            "--batteries",
            args.batteries,
            "--connected",
            args.connected,
            "--transport",
            args.transport,
            "--profile",
            str(args.profile),
        ]
        if args.caps_word:
            command.append("--caps-word")
        if args.touch_panel:
            command.append("--touch-panel")
        if args.device_page:
            command.append("--device-page")
        if args.flash_page:
            command.append("--flash-page")
        run(command)
        raw_to_png(raw_path, args.output, args.scale)
    print(args.output)


def ask(prompt: str, default: str) -> str:
    value = input(f"{prompt} [{default}]: ").strip()
    return value if value else default


def ask_choice(prompt: str, choices: list[str], default: str) -> str:
    choice_text = "/".join(choices)
    while True:
        value = ask(f"{prompt} ({choice_text})", default).lower()
        if value in choices:
            return value
        print(f"Choose one of: {choice_text}")


def run_interactive(args: argparse.Namespace) -> argparse.Namespace:
    print("Prospector Operator status renderer")
    args.layout = ask_choice("Screen/theme", ["operator"], args.layout)
    args.wpm = int(ask("WPM", str(args.wpm)))
    args.mods = ask("Active modifiers, comma-separated", args.mods)
    args.caps_word = ask_choice("Caps word active", ["no", "yes"], "yes" if args.caps_word else "no") == "yes"
    args.modifier_order = ask("Modifier order", args.modifier_order).upper()
    args.layer = ask("Layer display name", args.layer)
    args.layer_count = int(ask("Layer count / horizontal bars", str(args.layer_count)))
    args.active_layer = int(ask("Active layer index, 0-based", str(args.active_layer)))
    args.batteries = ask("Peripheral batteries, comma-separated", args.batteries)
    battery_count = len(parse_csv_ints(args.batteries))
    args.connected = ask("Peripheral connection states, comma-separated", ",".join(["yes"] * battery_count))
    args.transport = ask_choice("Selected output", ["usb", "ble"], args.transport)
    args.profile_count = int(ask("BLE profile count", str(args.profile_count)))
    args.profile = int(ask("Selected BLE profile, 1-based", str(args.profile)))
    args.scale = int(ask("PNG scale", str(args.scale)))
    args.output = Path(ask("Output PNG path", str(args.output)))
    return args


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Render a Prospector Operator status screen PNG.")
    parser.add_argument("--interactive", "-i", action="store_true", help="Prompt for every visible screen value")
    parser.add_argument(
        "--prepare-deps",
        action="store_true",
        help="Download west/ZMK/LVGL into an ignored cache for the LVGL renderer",
    )
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE_DIR)
    parser.add_argument("--lvgl-dir", type=Path, default=None, help="Use an existing local LVGL checkout")
    parser.add_argument("--layout", choices=["operator"], default="operator")
    parser.add_argument("--wpm", type=int, default=67)
    parser.add_argument("--mods", default="shift", help="Comma-separated active modifiers: gui,alt,ctrl,shift")
    parser.add_argument("--caps-word", action="store_true", help="Render shift using the caps word color")
    parser.add_argument("--touch-panel", action="store_true", help="Show the touch settings panel")
    parser.add_argument("--device-page", action="store_true", help="Show the device information page (host mock data)")
    parser.add_argument("--flash-page", action="store_true", help="Show the bootloader confirmation page (no real reboot)")
    parser.add_argument("--modifier-order", default="GACS")
    parser.add_argument("--batteries", default="65,11", help="Comma-separated peripheral battery levels")
    parser.add_argument("--connected", default="yes,yes", help="Comma-separated peripheral connection states")
    parser.add_argument("--transport", choices=["usb", "ble"], default="usb")
    parser.add_argument("--profile", type=int, default=1, help="Selected BLE profile, 1-based")
    parser.add_argument("--profile-count", type=int, default=4)
    parser.add_argument("--layer", default="Base")
    parser.add_argument("--layer-count", type=int, default=5)
    parser.add_argument("--active-layer", type=int, default=0)
    parser.add_argument("--scale", type=int, default=3)
    parser.add_argument("--output", type=Path, default=Path("docs/images/operator.png"))
    return parser


def main() -> None:
    parser = build_arg_parser()
    args = parser.parse_args()
    if args.prepare_deps:
        if shutil.which("git") is None:
            parser.error("git is required to prepare the dependency cache")
        prepare_deps(args.cache_dir)
        return

    if args.interactive or (len(sys.argv) == 1 and sys.stdin.isatty()):
        args = run_interactive(args)
    if args.profile < 1 or args.profile > args.profile_count:
        parser.error("--profile must be between 1 and --profile-count")
    if args.active_layer < 0 or args.active_layer >= args.layer_count:
        parser.error("--active-layer must be between 0 and --layer-count - 1")

    render_operator(args, parser)


if __name__ == "__main__":
    main()
