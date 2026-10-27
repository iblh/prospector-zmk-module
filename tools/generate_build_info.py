#!/usr/bin/env python3
"""Identify working source/config contents, including uncommitted changes."""
import argparse
from datetime import datetime
import hashlib
from pathlib import Path
import subprocess
from zoneinfo import ZoneInfo

parser = argparse.ArgumentParser()
parser.add_argument('--module', type=Path, required=True)
parser.add_argument('--config', type=Path, required=True)
parser.add_argument('--autoconfig', type=Path, required=True)
parser.add_argument('--zmk', type=Path, required=True)
parser.add_argument('--zephyr', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
digest = hashlib.sha256()
for label, root in [('module', args.module), ('config', args.config)]:
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if any(part.startswith('.') or part == '__pycache__' for part in relative.parts):
            continue
        if not path.is_file() or path.suffix not in {'.c', '.h', '.py', '.conf', '.overlay', '.keymap', '.dtsi', '.yml', '.yaml', '.txt'}:
            # Kconfig files have no suffix.
            if not path.is_file() or not path.name.startswith('Kconfig'):
                continue
        if label == 'module' and relative.parts[0] in {'docs', 'tools'} and path.name not in {
            'compress_lvgl_fonts.py', 'generate_build_info.py'}:
            continue
        digest.update(f'{label}/{relative.as_posix()}\0'.encode())
        digest.update(path.read_bytes())
digest.update(args.autoconfig.read_bytes())
for root in [args.zmk, args.zephyr]:
    revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD']).strip()
    digest.update(revision)
    digest.update(subprocess.check_output(['git', '-C', str(root), 'diff', 'HEAD', '--', '*.c', '*.h', 'Kconfig*', 'CMakeLists.txt']))
stamp = datetime.now(ZoneInfo('America/Los_Angeles')).strftime('%m-%d %H:%M')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text('/* Generated from working source; not a Git commit ID. */\n'
                       '#pragma once\n'
                       f'#define PROSPECTOR_SOURCE_ID "{digest.hexdigest()[:6].upper()}"\n'
                       f'#define PROSPECTOR_BUILD_TIME "{stamp}"\n')
