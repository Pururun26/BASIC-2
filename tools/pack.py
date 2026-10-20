#!/usr/bin/env python3
"""pack.py — склеить main.b2 + $INCLUDE в единый картридж .b2.

Использование:
    python pack.py main.b2                  # → cart.b2 (рядом с main.b2)
    python pack.py main.b2 -o snake.b2      # → snake.b2
    python pack.py carts/snake/main.b2 -o carts/snake.b2
"""

import sys
import re
import os

INCLUDE_RE = re.compile(r'^\s*\$INCLUDE\s+"([^"]+)"', re.IGNORECASE)


def expand(path, seen, out):
    path = os.path.normpath(path)
    if path in seen:
        return
    seen.add(path)

    try:
        with open(path, 'r', encoding='utf-8-sig') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"error: cannot open {path}", file=sys.stderr)
        sys.exit(1)

    directory = os.path.dirname(path) or '.'
    out.append(f"' === {path} ===")

    for line in lines:
        m = INCLUDE_RE.match(line)
        if m:
            inc = m.group(1)
            if not os.path.isabs(inc):
                inc = os.path.join(directory, inc)
            expand(inc, seen, out)
        else:
            out.append(line.rstrip('\r\n'))

    out.append('')


def main():
    args = sys.argv[1:]
    if not args:
        print(f"usage: {sys.argv[0]} <main.b2> [-o output.b2]", file=sys.stderr)
        return 2

    src = None
    dst = None
    i = 0
    while i < len(args):
        if args[i] == '-o':
            if i + 1 >= len(args):
                print("error: -o требует аргумент", file=sys.stderr)
                return 2
            dst = args[i + 1]
            i += 2
        elif src is None:
            src = args[i]
            i += 1
        else:
            print(f"error: лишний аргумент: {args[i]}", file=sys.stderr)
            return 2

    if src is None:
        print("error: укажи входной файл (main.b2)", file=sys.stderr)
        return 2

    # дефолт: cart.b2 рядом с main.b2
    if dst is None:
        dst = os.path.join(os.path.dirname(os.path.abspath(src)), 'cart.b2')

    out = []
    expand(src, set(), out)
    text = '\n'.join(out).rstrip() + '\n'

    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(f"packed -> {dst}", file=sys.stderr)

    return 0


if __name__ == '__main__':
    sys.exit(main())