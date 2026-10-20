#include "b2_api.h"
#include "font3x4.h"
#include "raylib.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* ============================================================
 *  Внутреннее состояние консоли
 * ============================================================ */
static unsigned char fb[B2_WIDTH * B2_HEIGHT];
static Color   palette[B2_COLOR_COUNT];
static Image     fb_img;
static Texture2D fb_tex;

static int cursor_y = 0;

static int prev_btn = 0;
static int curr_btn = 0;

/* ============================================================
 *  Жизненный цикл
 * ============================================================ */
void b2_init(void) {
    palette[0] = GetColor(B2_DEFAULT_COLOR_0);
    palette[1] = GetColor(B2_DEFAULT_COLOR_1);

    memset(fb, 0, sizeof fb);
    cursor_y = 0;
    prev_btn = curr_btn = 0;

    fb_img = GenImageColor(B2_WIDTH, B2_HEIGHT, palette[0]);
    fb_tex = LoadTextureFromImage(fb_img);
    SetTextureFilter(fb_tex, TEXTURE_FILTER_POINT);
}

void b2_close(void) {
    UnloadTexture(fb_tex);
    UnloadImage(fb_img);
}

/* ============================================================
 *  Кадр
 * ============================================================ */
void b2_clear(int color) {
    memset(fb, color & 1, sizeof fb);
    cursor_y = 0;
}

void b2_present(void) {
    Color* dst = (Color*)fb_img.data;
    for (int i = 0; i < B2_WIDTH * B2_HEIGHT; i++)
        dst[i] = palette[fb[i] & 1];
    UpdateTexture(fb_tex, fb_img.data);

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int scale = sw / B2_WIDTH < sh / B2_HEIGHT ? sw / B2_WIDTH : sh / B2_HEIGHT;
    if (scale < 1) scale = 1;
    int dw = B2_WIDTH * scale, dh = B2_HEIGHT * scale;
    int ox = (sw - dw) / 2, oy = (sh - dh) / 2;

    DrawTexturePro(fb_tex,
        (Rectangle){ 0, 0, B2_WIDTH, B2_HEIGHT },
        (Rectangle){ ox, oy, dw, dh },
        (Vector2){ 0, 0 }, 0.0f, WHITE);
}

/* ============================================================
 *  Пиксели
 * ============================================================ */
void b2_pset(int x, int y, int c) {
    if ((unsigned)x < B2_WIDTH && (unsigned)y < B2_HEIGHT)
        fb[y * B2_WIDTH + x] = c & 1;
}

int b2_pget(int x, int y) {
    if ((unsigned)x < B2_WIDTH && (unsigned)y < B2_HEIGHT)
        return fb[y * B2_WIDTH + x] & 1;
    return 0;
}

/* ============================================================
 *  Примитивы: RECT (залитый), OVAL (залитый)
 * ============================================================ */
void b2_rect(int x, int y, int w, int h, int c) {
    if (w <= 0 || h <= 0) return;

    int x0 = x,        y0 = y;
    int x1 = x + w,    y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > B2_WIDTH)  x1 = B2_WIDTH;
    if (y1 > B2_HEIGHT) y1 = B2_HEIGHT;
    if (x0 >= x1 || y0 >= y1) return;

    unsigned char cc = (unsigned char)(c & 1);
    for (int yy = y0; yy < y1; yy++) {
        unsigned char* row = &fb[yy * B2_WIDTH];
        for (int xx = x0; xx < x1; xx++) row[xx] = cc;
    }
}

void b2_oval(int x, int y, int w, int h, int c) {
    if (w <= 0 || h <= 0) return;

    /* Эллипс в bounding box (x, y, w, h):
       центр (cx, cy), радиусы (rx, ry).
       Для каждой строки py считаем |dy| = (py - cy) / ry,
       если |dy| > 1 — пропускаем, иначе x-диапазон = cx ± rx * sqrt(1 - dy²). */
    double cx = x + w / 2.0;
    double cy = y + h / 2.0;
    double rx = w / 2.0;
    double ry = h / 2.0;
    if (rx < 0.5) rx = 0.5;
    if (ry < 0.5) ry = 0.5;

    int y0 = y, y1 = y + h;
    if (y0 < 0) y0 = 0;
    if (y1 > B2_HEIGHT) y1 = B2_HEIGHT;

    unsigned char cc = (unsigned char)(c & 1);
    for (int py = y0; py < y1; py++) {
        double dy = (py + 0.5 - cy) / ry;
        if (dy < -1.0 || dy > 1.0) continue;
        double dx = sqrt(1.0 - dy * dy) * rx;

        int xs = (int)(cx - dx + 0.5);
        int xe = (int)(cx + dx + 0.5);
        if (xs < 0) xs = 0;
        if (xe > B2_WIDTH) xe = B2_WIDTH;
        if (xs >= xe) continue;

        unsigned char* row = &fb[py * B2_WIDTH];
        for (int px = xs; px < xe; px++) row[px] = cc;
    }
}

void b2_line(int x1, int y1, int x2, int y2, int c) {
    /* Алгоритм Брезенхэма. Клиппинг — через b2_pset (per-pixel). */
    int dx =  abs(x2 - x1);
    int sx = (x1 < x2) ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        b2_pset(x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

/* ============================================================
 *  Текст
 * ============================================================ */
static int draw_char(int x, int y, char ch, int c) {
    unsigned char u = (unsigned char)ch;
    if (u >= 'a' && u <= 'z') u -= 32;    /* a→A */

    int idx = u - FONT_FIRST;
    if (idx < 0 || idx >= FONT_COUNT) idx = '?' - FONT_FIRST;

    for (int row = 0; row < FONT_CELL_H; row++) {
        unsigned char bits = b2_font[idx][row];
        for (int col = 0; col < FONT_CELL_W; col++) {
            if ((bits >> (FONT_CELL_W - 1 - col)) & 1)
                b2_pset(x + col, y + row, c);
        }
    }
    return b2_font_w[idx];
}

void b2_text(int x, int y, const char* s, int c) {
    int cx = x, cy = y;
    for (const unsigned char* p = (const unsigned char*)s; p && *p; p++) {
        if (*p == '\n') { cx = x; cy += FONT_CELL_H + 1; continue; }
        int w = draw_char(cx, cy, (char)*p, c);
        cx += w + 1;
        if (cx + FONT_CELL_W > B2_WIDTH) { cx = x; cy += FONT_CELL_H + 1; }
    }
}

void b2_print(const char* s, int c) {
    b2_text(0, cursor_y, s, c);
    cursor_y += FONT_CELL_H + 1;              /* 4 + 1 = 5 px */
    if (cursor_y + FONT_CELL_H > B2_HEIGHT) cursor_y = 0;
}

void b2_reset_cursor(void) {
    cursor_y = 0;
}

/* ============================================================
 *  Ввод
 * ============================================================ */
static int host_read_buttons(void) {
    int b = 0;
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) b |= 1 << 0;   /* LEFT */
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) b |= 1 << 1;   /* RIGHT */
    if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) b |= 1 << 2;   /* UP */
    if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) b |= 1 << 3;   /* DOWN */
    if (IsKeyDown(KEY_Z) || IsKeyDown(KEY_J)) b |= 1 << 4;       /* A */
    if (IsKeyDown(KEY_X) || IsKeyDown(KEY_K)) b |= 1 << 5;       /* B */
    return b;
}

void b2_poll_buttons(void) {
    prev_btn = curr_btn;
    curr_btn = host_read_buttons();
}

int b2_btn(int i) {
    if (i < 0 || i >= 6) return 0;
    return (curr_btn & (1 << i)) ? -1 : 0;
}

int b2_btnp(int i) {
    if (i < 0 || i >= 6) return 0;
    int edge = curr_btn & ~prev_btn;
    return (edge & (1 << i)) ? -1 : 0;
}

/* ============================================================
 *  Спрайты: 96 × 8×8, 1 бит на пиксель (8 байт на спрайт)
 *
 *  SPRDATA принимает ASCII-арт:
 *    включено:  █ (U+2588, UTF-8), '1', '#'
 *    выключено: '0', '.', ' '
 *    прочее (пробелы, \r, неизвестные) игнорируется
 *  Ровно 8 строк × 8 пикселей. Файл .b2 — UTF-8 без BOM.
 * ============================================================ */
#define B2_NUM_SPRITES 256
static unsigned char sprites[B2_NUM_SPRITES][8];

void b2_sprdata(int id, const char* art) {
    if (id < 0 || id >= B2_NUM_SPRITES || !art) return;

    /* Сброс: повторный SPRDATA не должен оставлять «хвост» от старого */
    memset(sprites[id], 0, 8);

    int row = 0, col = 0;
    unsigned char cur = 0;

    for (const unsigned char* p = (const unsigned char*)art; *p; ) {
        /* Конец строки */
        if (*p == '\n') {
            if (col > 0) {
                if (row < 8) sprites[id][row] = cur;
                row++;
                col = 0;
                cur = 0;
            }
            p++;
            continue;
        }
        /* CR — пропускаем (поддержка CRLF из Windows) */
        if (*p == '\r') { p++; continue; }

        int on    = 0;
        int bytes = 1;

        if (p[0] == '1' || p[0] == '#') {
            on = 1;
        } else if (p[0] == 0xE2 && p[1] == 0x96 && p[2] == 0x88) {
            /* █ = U+2588 FULL BLOCK — 3 байта в UTF-8 */
            on    = 1;
            bytes = 3;
        } else if (p[0] == '0' || p[0] == '.' || p[0] == ' ') {
            on = 0;
        } else {
            /* неизвестный символ — пропускаем, не считая пикселем */
            p++;
            continue;
        }

        if (row < 8 && col < 8 && on)
            cur |= (unsigned char)(0x80 >> col);

        col++;
        p += bytes;
    }

    /* Последняя строка, если файл не заканчивается \n */
    if (col > 0 && row < 8) sprites[id][row] = cur;
}

void b2_spr(int id, int x, int y) {
    if (id < 0 || id >= B2_NUM_SPRITES) return;

    for (int r = 0; r < 8; r++) {
        unsigned char bits = sprites[id][r];
        for (int c = 0; c < 8; c++) {
            if ((bits >> (7 - c)) & 1)
                b2_pset(x + c, y + r, 1);
        }
    }
}

/* ============================================================
 *  Звук: 4 канала.
 *    Канал 0: pulse 12.5%
 *    Канал 1: pulse 25%
 *    Канал 2: triangle
 *    Канал 3: noise
 *  Один звук на канал. Новый TONE вытесняет старый.
 * ============================================================ */
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define TONE_SAMPLE_RATE 44100

typedef struct {
    Sound sound;
    int   used;
} ToneChannel;

static ToneChannel tone_channels[4];
static int         tone_seeded = 0;

static void tone_gen_pulse(short* s, int n, float freq, int vol, float duty) {
    for (int i = 0; i < n; i++) {
        float t     = (float)i / TONE_SAMPLE_RATE;
        float phase = fmodf(t * freq, 1.0f);
        float v     = (phase < duty) ? 1.0f : -1.0f;
        s[i] = (short)(v * vol * 327.67f);
    }
}

static void tone_gen_triangle(short* s, int n, float freq, int vol) {
    for (int i = 0; i < n; i++) {
        float t     = (float)i / TONE_SAMPLE_RATE;
        float phase = fmodf(t * freq, 1.0f);
        float v     = 2.0f * fabsf(2.0f * phase - 1.0f) - 1.0f;
        s[i] = (short)(v * vol * 327.67f);
    }
}

static void tone_gen_noise(short* s, int n, int vol) {
    for (int i = 0; i < n; i++) {
        float v = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        s[i] = (short)(v * vol * 327.67f);
    }
}

static void tone_stop_channel(int ch) {
    if (ch < 0 || ch > 3) return;
    if (tone_channels[ch].used) {
        StopSound(tone_channels[ch].sound);
        UnloadSound(tone_channels[ch].sound);
        tone_channels[ch].used = 0;
    }
}

void b2_tone(int freq, int dur, int vol, int channel) {
    if (channel < 0 || channel > 3) return;

    /* freq=0 или dur=0 или vol=0 → остановить канал */
    if (freq <= 0 || dur <= 0 || vol <= 0) {
        tone_stop_channel(channel);
        return;
    }
    if (!IsAudioDeviceReady()) return;

    if (!tone_seeded) { srand((unsigned)time(NULL)); tone_seeded = 1; }

    /* 1 кадр = 1/30 сек */
    float duration_sec = dur / 30.0f;
    int   n            = (int)(TONE_SAMPLE_RATE * duration_sec);
    if (n <= 0) return;

    short* samples = (short*)malloc(n * sizeof(short));
    if (!samples) return;

    switch (channel) {
        case 0: tone_gen_pulse   (samples, n, (float)freq, vol, 0.125f); break;
        case 1: tone_gen_pulse   (samples, n, (float)freq, vol, 0.250f); break;
        case 2: tone_gen_triangle(samples, n, (float)freq, vol);         break;
        case 3: tone_gen_noise   (samples, n, vol);                      break;
    }

    Wave wave = {
        .frameCount = (unsigned int)n,
        .sampleRate = TONE_SAMPLE_RATE,
        .sampleSize = 16,
        .channels   = 1,
        .data       = samples,
    };
    Sound snd = LoadSoundFromWave(wave);
    free(samples);
    if (snd.frameCount == 0) return;

    /* Вытесняем старый звук на этом канале */
    tone_stop_channel(channel);

    tone_channels[channel].sound = snd;
    tone_channels[channel].used  = 1;
    PlaySound(snd);
}