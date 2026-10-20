#!/usr/bin/env python3
"""gen_font.py — 3×4 pixel font PNG → src/font4x6.h.

Usage:
    python tools/gen_font.py font.png src/font4x6.h

PNG: 48 × 16, 16 columns × 4 rows of 3×4 cells.
     64 glyphs (ASCII 32..95).
     Black background, white pixels (brightness > 128).
"""

import sys
from PIL import Image

CELL_W  = 3
CELL_H  = 4
PER_ROW = 16
FIRST   = 32
COUNT   = 64


def is_fg(px, x, y):
    """Пиксель — часть глифа, если он светлый (сумма RGB > 384)."""
    r, g, b, a = px[x, y]
    if a < 128:           # прозрачный = фон
        return False
    return (r + g + b) > 384


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <font.png> <out.h>", file=sys.stderr)
        return 1

    src, dst = sys.argv[1], sys.argv[2]
    img = Image.open(src).convert('RGBA')
    w, h = img.size
    px  = img.load()

    exp_w = PER_ROW * CELL_W            # 48
    exp_h = (COUNT // PER_ROW) * CELL_H # 16
    if (w, h) != (exp_w, exp_h):
        print(f"warning: ожидалось {exp_w}×{exp_h}, получено {w}×{h}",
              file=sys.stderr)

    bitmap = [[0] * CELL_H for _ in range(COUNT)]
    widths = [1] * COUNT

    for idx in range(COUNT):
        gx = (idx % PER_ROW) * CELL_W
        gy = (idx // PER_ROW) * CELL_H

        min_x, max_x = CELL_W, -1
        for py in range(CELL_H):
            for pxx in range(CELL_W):
                sx, sy = gx + pxx, gy + py
                if sx < w and sy < h and is_fg(px, sx, sy):
                    if pxx < min_x: min_x = pxx
                    if pxx > max_x: max_x = pxx

        if max_x < 0:
            widths[idx] = 1               # пустой (space) → advance 1
            continue

        gw = max_x - min_x + 1
        widths[idx] = gw

        for py in range(CELL_H):
            for pxx in range(gw):
                sx, sy = gx + min_x + pxx, gy + py
                if sx < w and sy < h and is_fg(px, sx, sy):
                    bitmap[idx][py] |= (1 << (CELL_W - 1 - pxx))

    # space — фиксированная ширина 2 (иначе пустая клетка даст 1, слова слипнутся)
    widths[0] = 2
    
    with open(dst, 'w', encoding='utf-8') as f:
        f.write(f'/* Auto-generated from {src} — do not edit. */\n')
        f.write('#ifndef FONT4X6_H\n#define FONT4X6_H\n\n')
        f.write(f'#define FONT_CELL_W {CELL_W}\n')
        f.write(f'#define FONT_CELL_H {CELL_H}\n')
        f.write(f'#define FONT_FIRST  {FIRST}\n')
        f.write(f'#define FONT_COUNT  {COUNT}\n\n')

        f.write(f'static const unsigned char b2_font[{COUNT}][{CELL_H}] = {{\n')
        for idx in range(COUNT):
            ch = idx + FIRST
            tag = f"'{chr(ch)}'" if 33 <= ch <= 126 else f'#{ch}'
            row = ','.join(f'0x{bitmap[idx][r]:02X}' for r in range(CELL_H))
            f.write(f'    /* {tag:>4} */ {{{row}}},\n')
        f.write('};\n\n')

        f.write(f'static const unsigned char b2_font_w[{COUNT}] = {{\n')
        for i in range(COUNT):
            if i % 16 == 0: f.write('    ')
            f.write(f'{widths[i]},')
            if i % 16 == 15: f.write('\n')
        f.write('};\n\n#endif\n')

    print(f"OK: {dst}")
    print(f"  {CELL_W}×{CELL_H} cells, {COUNT} glyphs (ASCII {FIRST}..{FIRST+COUNT-1})")
    print(f"  widths[32..47] = {widths[0:16]}")
    return 0


if __name__ == '__main__':
    sys.exit(main())