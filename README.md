# BASIC-2

A tiny fantasy console with its own BASIC-like language. Built for making small 2D games fast.

- 96×96 virtual screen, 4× window (384×384), 2-color palette
- 30 FPS fixed timestep, letterboxed, nearest-neighbour scaling
- 256 sprites (8×8, 1 bit per pixel), defined inline in ASCII art
- 4-channel tone synthesis (WASM-4 style)
- 6 buttons — cross + A/B
- Cartridges — plain text `.b2` files, readable in any editor
- Basic Script — own interpreter, no Lua/Python/JS
- `$INCLUDE` — multi-file carts, recursive, relative paths
- `pack.py` — packs a modular project into one `.b2` for sharing
- VS Code syntax highlighting extension included
- Single executable, no external DLLs beyond raylib
- Windows build (MSYS2 UCRT64 + MinGW-w64)

---

## Building

### Requirements

- **MSYS2 UCRT64** — [msys2.org](https://www.msys2.org/)
- **MinGW-w64 GCC** — `pacman -S mingw-w64-ucrt-x86_64-gcc make`
- **raylib 6.0** — pre-built binary included in `raylib/`

### Build

```bash
make              # → build/b2.exe
make clean        # снести build/
```

The raylib Windows release is included in this repository, so the project builds out of the box on MSYS2 UCRT64. For other platforms, replace `raylib/raylib-6.0_win64_mingw-w64/` with the matching release from [raylib releases](https://github.com/raysan5/raylib/releases) and adjust `Makefile` (raylib flags — `-lraylib -lopengl32 -lgdi32 -lwinmm -lm`).

---

## Command line

```
b2.exe <cartridge.b2>     run a cartridge
b2.exe                    shows usage
```

Example:

```bash
./build/b2.exe carts/snake.b2
./build/b2.exe scripts/main.b2       # dev mode, $INCLUDE works
```

---

## Cartridges

A cartridge is a plain text `.b2` file — UTF-8, no BOM. Contains BASIC-2 code.

```basic
' ============================================================
' MY GAME — cartridge for BASIC-2
' ============================================================

GLOBAL PX, PY

PX = 48
PY = 48

SPRDATA 0, """
00000000
00####00
0######0
0##00##0
0######0
0##00##0
0##00##0
00000000
"""

SUB UPDATE
  IF BTN(0) THEN PX = PX - 1
  IF BTN(1) THEN PX = PX + 1
  IF BTN(2) THEN PY = PY - 1
  IF BTN(3) THEN PY = PY + 1

  CLS 0
  SPR 0, PX, PY
END SUB
```

### How the engine finds the code

- Top-level code (before first `SUB`) — executed **once** at load.
- `SUB UPDATE` — called **every frame** (30 fps).
- `SUB INIT` — optional, **not called automatically**. If you want restart — `CALL INIT` from inside `UPDATE` or by button.

### Multi-file carts

```basic
$INCLUDE "config.b2"
$INCLUDE "sprites.b2"
$INCLUDE "update.b2"
```

`$INCLUDE` is expanded recursively at load — paths relative to the file that contains the directive. Up to 16 levels deep.

Recommended layout:

```
carts/
└── snake/
    ├── main.b2      ← entry point: $INCLUDE + init
    ├── config.b2    ← GLOBAL, DIM GLOBAL
    ├── sprites.b2   ← SPRDATA
    └── update.b2    ← SUB UPDATE
```

### Packing a cart

For release — pack a modular project into one `.b2`:

```bash
python tools/pack.py carts/snake/main.b2                # → cart.b2 (рядом)
python tools/pack.py carts/snake/main.b2 -o snake.b2    # → snake.b2
```

Result — single `.b2` with all `$INCLUDE` expanded and deduplicated. Ready to share.

---

## Running

**F11** — toggle fullscreen.

**ESC** or window close — exit.

Pause is not provided by the console. If a game needs it, implement in `SUB UPDATE` via `BTNP`.

---

## Architecture

```
b2.exe
src/
├── b2.c              ← main() — only orchestration
├── b2_conf.h         ← screen, palette, fps, sprites count
├── b2_api.h          ← public C API of the console
├── b2_api.c          ← framebuffer, render, text, sprites, sound
├── b2_api_bs.h       ← bs binding declaration
├── b2_api_bs.c       ← nat_* functions, b2_register_bs_api
└── font4x6.h         ← 4×6 bitmap font
bs/                   ← Basic Script interpreter (lexer, parser, VM)
raylib/               ← raylib 6.0 prebuilt
tools/
├── pack.py           ← cart packer
└── vscode-basic2/    ← VS Code syntax highlighting
carts/                ← games
```

`b2_api.c` knows nothing about `bs_State`. `b2_api_bs.c` knows nothing about raylib. `b2.c` only glues them together.

---

## API

All console commands and functions. Nothing else exists in the language.

### Screen

```basic
CLS [c]                  ' clear screen (default c = 0)
PSET x, y, c             ' set pixel
LINE x1, y1, x2, y2, c   ' line (Bresenham)
RECT x, y, w, h, c       ' filled rectangle
OVAL x, y, w, h, c       ' filled oval in bounding box
```

**Border** — two RECTs:

```basic
RECT 0, 0, 96, 96, 1
RECT 1, 1, 94, 94, 0
```

**Circle** — `OVAL x-r, y-r, r*2, r*2, c`.

Coordinates can be anything — off-screen pixels are silently clipped. No boundary checks needed.

### Pixels

```basic
PGET(x, y)               ' → 0/1, 0 if off-screen
```

### Sprites

```basic
SPRDATA id, """..."""    ' define sprite id (0…255)
SPR id, x, y             ' draw sprite at (x, y)
```

`SPRDATA` parses ASCII art:

- **on**: `█` (U+2588), `1`, `#`
- **off**: `0`, `.`, space
- **anything else** ignored
- exactly **8 rows × 8 pixels**

### Text

```basic
PRINT "string"                ' to current cursor
PRINT "string", x, y          ' at (x, y), color 1
PRINT "string", x, y, c       ' at (x, y), color c
PRINT a, b, c                 ' comma-separated (space between)
PRINT a; b; c                 ' semicolon-separated (no space)
```

Font 4×6 — 24 × 16 characters on screen. `PRINT` without coordinates uses **cursor** — advances 7 px down after each line. `CLS` resets cursor.

### Input

```basic
BTN(i)                   ' → -1 held, 0 otherwise
BTNP(i)                  ' → -1 just pressed this frame, 0 otherwise
```

| # | Button | Keys |
|---|---|---|
| 0 | LEFT | ← / A |
| 1 | RIGHT | → / D |
| 2 | UP | ↑ / W |
| 3 | DOWN | ↓ / S |
| 4 | A | Z / J |
| 5 | B | X / K |

### Sound

```basic
TONE freq, dur, vol, channel
```

| Arg | Meaning |
|---|---|
| `freq` | Hz (0…~15000), `0` = stop channel |
| `dur` | in **frames** (30 = 1 second) |
| `vol` | 0…100 |
| `channel` | 0…3 |

| Channel | Waveform |
|---|---|
| 0 | pulse 12.5% |
| 1 | pulse 25% |
| 2 | triangle |
| 3 | noise |

4 channels — 4 sounds simultaneously. A new tone on the same channel replaces the previous one.

Examples:

```basic
TONE 880, 4, 80, 0        ' beep (pulse 12.5%, ~133 ms)
TONE 110, 20, 100, 3      ' explosion (noise, ~666 ms)
TONE 0, 0, 0, 0           ' stop channel 0
```

---

## Basic Script

The console runs on **Basic Script** — its own interpreter, not Lua.

### Commands vs functions

**Commands** — statements, no parentheses, don't return value:

```basic
CLS 0
RECT 0, 0, 96, 96, 1
SPR 0, 10, 10
PRINT "HELLO"
TONE 440, 5, 80, 0
```

**Functions** — in expressions, with parentheses, return value:

```basic
IF BTN(0) THEN X = X - 1
Y = PGET(10, 10)
IF PGET(X, Y) = 1 THEN ...
```

Rule: **need a result — function, parentheses required. Need an action — command, no parentheses**. Classic BASIC since 1964.

### Variables

```basic
X = 5                          ' local (top-level or inside SUB)
GLOBAL PX, PY, SIZE            ' visible from SUB
```

Variables used in `SUB` **must** be in `GLOBAL`, otherwise they're `nil` inside the SUB.

### Arrays

```basic
DIM BX(100)                    ' local (top-level only)
DIM GLOBAL BY(100)             ' global, visible from SUB
```

Arrays used in `SUB` **must** be `DIM GLOBAL` — otherwise they're invisible inside SUB (same semantics as LowRes NX).

### Built-in functions

Available out of the box (part of the language, not the console):

**Math**: `ABS`, `SGN`, `INT`, `SQR`, `EXP`, `LOG`, `SIN`, `COS`, `TAN`, `MIN`, `MAX`, `PI`
**Random**: `RND`, `RND(n)`, `RANDOMIZE`
**Strings**: `LEN`, `LEFT$`, `RIGHT$`, `MID$`, `CHR$`, `ASC`, `STR$`, `VAL`

⚠️ Don't use `LEN`, `STR$`, `RND` as variable names — they shadow builtins.

### Control flow

```basic
IF x THEN ... END IF
IF x THEN ... ELSE ... END IF

FOR I = 0 TO 10 STEP 2 ... NEXT I
WHILE x ... WEND
DO ... LOOP
REPEAT ... UNTIL x

GOTO label
GOSUB label ... RETURN

SUB name(params) ... END SUB
CALL name(args)
EXIT SUB
```

---

## Example cart

`carts/snake/` — играбельная змейка. ~130 lines total, uses everything: sprites, sound, arrays, subs, `$INCLUDE`.

```bash
./build/b2.exe carts/snake/main.b2
```

Or packed:

```bash
python tools/pack.py carts/snake/main.b2 -o carts/snake.b2
./build/b2.exe carts/snake.b2
```

---

## VS Code

Syntax highlighting for `.b2` files. Local extension:

```
tools/vscode-basic2/
├── package.json
├── language-configuration.json
└── syntaxes/
    └── b2.tmLanguage.json
```

Install: `Ctrl+Shift+P` → **`Developer: Install Extension from Location...`** → `tools/vscode-basic2/`. Reload VS Code. `.b2` files highlight automatically.

If a theme ignores scopes, add to `.vscode/settings.json`:

```json
{
    "files.associations": { "*.b2": "basic2" }
}
```

---

## Known limitations

- No flip / scale / rotation for sprites.
- No tile maps (`MAP`, `TILES`).
- No transparency (2 colors — nothing to be transparent *to*).
- No binary sections in `.b2` — sprites are ASCII, music is `TONE` in code.
- No palette header in cartridge — palette is global for the whole console.
- `$INCLUDE` — works both at runtime and via `pack.py`.
- `TONE` calls `LoadSoundFromWave` on every invocation — don't call it every frame, only on events.