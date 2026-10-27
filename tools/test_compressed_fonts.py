#!/usr/bin/env python3
"""Round-trip every retained font glyph through the actual LVGL C RLE decoder."""
import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile
import re
from compress_lvgl_fonts import convert, repertoire

root = Path(__file__).resolve().parents[1]
lvgl = Path(sys.argv[1])
source = (lvgl / 'src/font/lv_font_fmt_txt.c').read_text()
header = (lvgl / 'src/font/lv_font_fmt_txt_private.h').read_text()
types = header[header.index('typedef enum'):header.index('} lv_font_fmt_rle_t;') + len('} lv_font_fmt_rle_t;')]
decoder = source[source.index('static inline uint8_t get_bits(', source.index('static void decompress(', 5000)):]
decoder = decoder[:decoder.index('#endif /*LV_USE_FONT_COMPRESSED*/')]
c = '#include <stdint.h>\n' + types + '\nstatic lv_font_fmt_rle_t font_rle;\n' + decoder
c += '\nvoid decode(const uint8_t *in, uint8_t *out, int n, int bpp) {rle_init(in,bpp); for(int i=0;i<n;i++)out[i]=rle_next();}\n'
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'decoder.c').write_text(c)
    subprocess.run(['cc', '-shared', '-fPIC', str(path / 'decoder.c'), '-o', str(path / 'decoder.so')], check=True)
    lib = ctypes.CDLL(str(path / 'decoder.so'))
    count = 0
    for font in sorted((root / 'boards/shields/prospector_adapter/src/fonts').glob('*.c')):
        packed_source, _, _, glyphs = convert(font.read_text(), repertoire(font.name, True))
        original_bitmap = re.search(r'glyph_bitmap\[\]\s*=\s*\{(.*?)\n\};', font.read_text(), re.S)[1]
        keep = repertoire(font.name, True)
        expected_cp = [int(x, 16) for x in re.findall(r'/\* U\+([0-9A-Fa-f]+)', original_bitmap)]
        expected_cp = [cp for cp in expected_cp if keep is None or cp >= 128 or cp in keep]
        mapped_cp = []
        for start, table in re.findall(r'\.range_start = (\d+).*?\.unicode_list = packed_unicode_(\d+)', packed_source):
            values = re.search(r'packed_unicode_' + table + r'\[\] = \{(.*?)\}', packed_source)[1]
            mapped_cp.extend(int(start) + int(x) for x in values.split(',') if x.strip())
        assert mapped_cp == expected_cp, font.name
        bitmap = re.search(r'glyph_bitmap\[\]\s*=\s*\{(.*?)\n\};', packed_source, re.S)[1]
        data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]+)', bitmap))
        offsets = [int(x) for x in re.findall(r'\.bitmap_index = (\d+)', packed_source)]
        assert len(offsets) == len(glyphs)
        for (pixels, _, width, bpp), offset in zip(glyphs, offsets):
            if not pixels:
                continue
            output = (ctypes.c_uint8 * len(pixels))()
            lib.decode(data[offset:] + b'\0\0', output, len(pixels), bpp)
            decoded = list(output)
            for i in range(width, len(decoded)):
                decoded[i] ^= decoded[i - width]
            assert decoded == pixels, (font.name, count)
            count += 1
    print(f'PASS: {count} glyphs match original pixels using LVGL C decoder')
