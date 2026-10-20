#pragma once

#include "b2_conf.h"

/* ============================================================
 *  BASIC-2 — публичный API консоли (C-сторона)
 *  Хост вызывает b2_init / b2_present / b2_poll_buttons,
 *  привязка к BS (b2_api_bs.c) использует b2_pset/b2_pget/b2_text/...
 *  Никакого raylib в этом заголовке — чистая абстракция консоли.
 * ============================================================ */

/* --- Жизненный цикл --- */
void b2_init(void);     /* палитра + framebuffer + текстура (после InitWindow) */
void b2_close(void);    /* освободить текстуру/образ */

/* --- Кадр --- */
void b2_clear(int color);    /* CLS */
void b2_present(void);       /* fb -> текстура -> окно (letterbox) */

/* --- Пиксели --- */
void b2_pset(int x, int y, int c);
int  b2_pget(int x, int y);  /* 0/1, за экраном — 0 */

/* --- Примитивы --- */
void b2_rect(int x, int y, int w, int h, int c);
void b2_oval(int x, int y, int w, int h, int c);

void b2_line(int x1, int y1, int x2, int y2, int c);

/* --- Текст --- */
void b2_text(int x, int y, const char* s, int c);   /* в координаты */
void b2_print(const char* s, int c);                /* с курсором, \n → вниз */
void b2_reset_cursor(void);

/* --- Ввод (опрос раз в кадр, до bs_call) --- */
void b2_poll_buttons(void);
int  b2_btn(int i);   /* -1 если зажата, иначе 0 */
int  b2_btnp(int i);  /* -1 если нажата в этом кадре, иначе 0 */

/* --- Спрайты --- */
void b2_sprdata(int id, const char* art);   /* art — многострочный ASCII, █/1/# and 0 */
void b2_spr(int id, int x, int y);

/* --- Звук --- */
void b2_tone(int freq, int dur, int vol, int channel);