#ifndef B2_CONF_H
#define B2_CONF_H

/* ============================================================
 *  BASIC-2 — конфигурация консоли
 *  Всё, что пользователь может захотеть поменять, — здесь.
 * ============================================================ */

/* --- Окно --- */
#define B2_WINDOW_WIDTH  384
#define B2_WINDOW_HEIGHT 384
#define B2_TITLE         "BASIC-2"
#define B2_FPS           30

/* --- Виртуальный экран (framebuffer) --- */
#define B2_WIDTH   96
#define B2_HEIGHT  96

/* --- Палитра --- */
#define B2_COLOR_COUNT 2

/* Цвета в формате 0xRRGGBBAA (raylib GetColor) */
#define B2_DEFAULT_COLOR_0  0x72A488FF   /* фон         — #72a488 */
#define B2_DEFAULT_COLOR_1  0x212C28FF   /* передний план — #212c28 */

#endif /* B2_CONF_H */