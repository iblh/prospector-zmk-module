#!/usr/bin/env python3
"""Subset fonts by UI role and losslessly pack bitmaps; leave sources intact.

RLE format follows lvgl/lv_font_conv's MIT-licensed lib/font/compress.js
(https://github.com/lvgl/lv_font_conv). Uses row XOR prefilter; no pixel quantization.
"""
import argparse
from pathlib import Path
import re


def pack(pixels, bpp):
    bits = []

    def put(value, width):
        bits.extend((value >> n) & 1 for n in reversed(range(width)))

    offset = 0
    while offset < len(pixels):
        value = pixels[offset]
        count = 1
        while offset + count < len(pixels) and pixels[offset + count] == value and count < 75:
            count += 1
        offset += count
        put(value, bpp)
        if count == 1:
            continue
        put(value, bpp)
        repeats = count - 1
        if repeats <= 10:
            for _ in range(repeats - 1):
                put(1, 1)
            put(0, 1)
        else:
            put(0x7ff, 11)
            put(repeats - 11, 6)
    bits.extend([0] * ((-len(bits)) % 8))
    return bytes(sum(bits[i + j] << (7 - j) for j in range(8)) for i in range(0, len(bits), 8))


def repertoire(name, uppercase=False):
    if name.startswith('Symbols_'):
        return set(map(ord, ' -%0123456789'))
    if name in {'FR_Medium_32.c', 'FR_Regular_30.c', 'FG_Medium_24.c', 'FG_Medium_26.c'}:
        return set(map(ord, ' -/%0123456789'))
    if name.startswith('DINish_Condensed_Semibold_'):
        return set(map(ord, ' CMD OPT CTRL SHFT WIN ALT GUI'))
    if uppercase and name in {'PPF_NarrowThin_64.c', 'DINish_Expanded_Light_36.c'}:
        return set(range(32, 127)) - set(range(ord('a'), ord('z') + 1))
    return None


def convert(source, keep=None):
    bpp = int(re.search(r'\.bpp = (\d+)', source)[1])
    bitmap = re.search(r'(glyph_bitmap\[\]\s*=\s*\{)(.*?)(\n\};)', source, re.S)
    values = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', bitmap[2]))
    result = bytearray()
    glyphs = []
    retained = []
    codepoints = [None] + [int(x, 16) for x in re.findall(r'/\* U\+([0-9A-Fa-f]+)', bitmap[2])]
    glyph_index = 0

    def glyph(match):
        nonlocal glyph_index
        text = match[0]
        fields = {k: int(v) for k, v in re.findall(r'\.(\w+)\s*=\s*(-?\d+)', text)}
        size = fields['box_w'] * fields['box_h']
        codepoint = codepoints[glyph_index]
        glyph_index += 1
        if keep is not None and codepoint is not None and codepoint < 128 and codepoint not in keep:
            return ''
        retained.append(codepoint)
        start = fields['bitmap_index']
        raw = values[start:start + (size * bpp + 7) // 8]
        per_byte = 8 // bpp
        pixels = [(raw[i // per_byte] >> (8 - bpp * (i % per_byte + 1))) & ((1 << bpp) - 1)
                  for i in range(size)]
        index = len(result)
        width = fields['box_w']
        filtered = [value ^ (pixels[i - width] if i >= width else 0)
                    for i, value in enumerate(pixels)]
        encoded = pack(filtered, bpp)
        glyphs.append((pixels, encoded, width, bpp))
        result.extend(encoded)
        return re.sub(r'(?<=bitmap_index = )\d+', str(index), text)

    source = re.sub(r'\{\s*\.bitmap_index\s*=.*?\},?', glyph, source)
    # Rebuild sparse cmaps so omitted glyphs also shed their descriptors.
    groups = []
    for gid, cp in enumerate(retained[1:], 1):
        if not groups or cp - groups[-1][0][1] > 65534:
            groups.append([])
        groups[-1].append((gid, cp))
    declarations = []
    maps = []
    for i, group in enumerate(groups):
        start = group[0][1]
        declarations.append(f'static const uint16_t packed_unicode_{i}[] = {{' +
                            ', '.join(str(cp - start) for _, cp in group) + '};')
        maps.append('{.range_start = %d, .range_length = %d, .glyph_id_start = %d, '
                    '.unicode_list = packed_unicode_%d, .glyph_id_ofs_list = NULL, '
                    '.list_length = %d, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}' %
                    (start, group[-1][1] - start + 1, group[0][0], i, len(group)))
    source = re.sub(r'static const lv_font_fmt_txt_cmap_t cmaps\[\]\s*=\s*\{.*?\};',
                    lambda _: '\n'.join(declarations) + '\nstatic const lv_font_fmt_txt_cmap_t cmaps[] = {' +
                    ',\n'.join(maps) + '};', source, flags=re.S)
    source = re.sub(r'\.cmap_num = \d+', f'.cmap_num = {len(groups)}', source)
    source = re.sub(r'static const uint16_t unicode_list_\d+\[\]\s*=\s*\{.*?\};', '', source, flags=re.S)
    # LVGL's bit reader may look one byte beyond the final encoded bits.
    result.append(0)
    rows = ['    ' + ', '.join(f'0x{x:02x}' for x in result[i:i + 16]) + ','
            for i in range(0, len(result), 16)]
    source = re.sub(r'(glyph_bitmap\[\]\s*=\s*\{).*?(\n\};)',
                    lambda m: m[1] + '\n' + '\n'.join(rows) + m[2], source, flags=re.S)
    source = source.replace('.bitmap_format = 0,', '.bitmap_format = 1,')
    source = '/* Generated UI subset and lossless RLE; retained glyph metrics and pixels unchanged. */\n' + source
    return source, len(values), len(result), glyphs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--uppercase-layer-fonts', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    before = after = 0
    for path in sorted(args.source.glob('*.c')):
        output, raw, packed, _ = convert(path.read_text(), repertoire(path.name, args.uppercase_layer_fonts))
        target = args.output / path.name
        if not target.exists() or target.read_text() != output:
            target.write_text(output)
        before += raw
        after += packed
    print(f'Font bitmaps: {before} -> {after} bytes (saved {before - after})')


if __name__ == '__main__':
    main()
