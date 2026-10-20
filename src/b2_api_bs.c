#include "b2_api_bs.h"
#include "b2_api.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 *  BS-нативы: тонкий слой поверх b2_api
 * ============================================================ */

/* CLS [c] */
static int nat_cls(bs_State* S) {
    int c = bs_argc(S) > 0 ? ((int)bs_arg_number(S, 0) & 1) : 0;
    b2_clear(c);
    return 0;
}

/* PSET x, y, c */
static int nat_pset(bs_State* S) {
    b2_pset((int)bs_arg_number(S, 0),
            (int)bs_arg_number(S, 1),
            (int)bs_arg_number(S, 2));
    return 0;
}

/* PGET(x, y) -> 0/1 */
static int nat_pget(bs_State* S) {
    int v = b2_pget((int)bs_arg_number(S, 0),
                    (int)bs_arg_number(S, 1));
    bs_push_number(S, v);
    return 1;
}

/* RECT x, y, w, h, c */
static int nat_rect(bs_State* S) {
    b2_rect((int)bs_arg_number(S, 0),
            (int)bs_arg_number(S, 1),
            (int)bs_arg_number(S, 2),
            (int)bs_arg_number(S, 3),
            (int)bs_arg_number(S, 4));
    return 0;
}

/* OVAL x, y, w, h, c */
static int nat_oval(bs_State* S) {
    b2_oval((int)bs_arg_number(S, 0),
            (int)bs_arg_number(S, 1),
            (int)bs_arg_number(S, 2),
            (int)bs_arg_number(S, 3),
            (int)bs_arg_number(S, 4));
    return 0;
}

/* LINE x1, y1, x2, y2, c */
static int nat_line(bs_State* S) {
    b2_line((int)bs_arg_number(S, 0),
            (int)bs_arg_number(S, 1),
            (int)bs_arg_number(S, 2),
            (int)bs_arg_number(S, 3),
            (int)bs_arg_number(S, 4));
    return 0;
}

/* PRINT "s"                 -> курсор
   PRINT "s", x, y           -> в (x, y), цвет 1
   PRINT "s", x, y, c        -> в (x, y), цвет c
   Разделители ',' ';' приходят маской — последний аргумент. */
static int nat_print(bs_State* S) {
    int argc = bs_argc(S);
    if (argc < 1) return 0;

    bs_Value vmask = bs_arg(S, argc - 1);
    const char* mask = (vmask.type == BS_STR) ? bs_string_chars(vmask.as.str) : "";
    int nmask = mask ? (int)strlen(mask) : 0;
    int nvals = argc - 1;

    int x = 0, y = 0, c = 1;
    int coord_mode = 0;
    int ntext = nvals;

    /* Хвост из 2–3 чисел после строки = координаты [+ цвет] */
    if (nvals >= 3) {
        int tail = 0;
        while (tail < nvals && bs_arg(S, nvals - 1 - tail).type == BS_NUM) tail++;
        if ((tail == 2 || tail == 3) && nvals - tail >= 1) {
            if (bs_arg(S, nvals - tail - 1).type == BS_STR) {
                coord_mode = 1;
                x = (int)bs_arg_number(S, nvals - tail);
                y = (int)bs_arg_number(S, nvals - tail + 1);
                if (tail == 3) c = (int)bs_arg_number(S, nvals - 1) & 1;
                ntext = nvals - tail;
            }
        }
    }

    static char buf[256];
    int bi = 0, mi = 0;
    for (int i = 0; i < ntext; i++) {
        if (i > 0 && mi < nmask) {
            if (mask[mi++] == ',' && bi < (int)sizeof(buf) - 1)
                buf[bi++] = ' ';
        }
        bs_Value v = bs_arg(S, i);
        if (v.type == BS_STR) {
            const char* sc = bs_string_chars(v.as.str);
            while (sc && *sc && bi < (int)sizeof(buf) - 1) buf[bi++] = *sc++;
        } else if (v.type == BS_NUM) {
            char tmp[32];
            snprintf(tmp, sizeof tmp, "%.14g", v.as.num);
            for (const char* p = tmp; *p && bi < (int)sizeof(buf) - 1; p++)
                buf[bi++] = *p;
        }
    }
    buf[bi] = 0;

    if (coord_mode) b2_text(x, y, buf, c);
    else            b2_print(buf, c);
    return 0;
}

/* BTN(i)  -> -1/0 */
static int nat_btn(bs_State* S) {
    bs_push_number(S, b2_btn((int)bs_arg_number(S, 0)));
    return 1;
}

/* BTNP(i) -> -1/0 (edge) */
static int nat_btnp(bs_State* S) {
    bs_push_number(S, b2_btnp((int)bs_arg_number(S, 0)));
    return 1;
}

/* SPRDATA id, """...""" */
static int nat_sprdata(bs_State* S) {
    int id = (int)bs_arg_number(S, 0);
    const char* art = bs_arg_string(S, 1);
    b2_sprdata(id, art);
    return 0;
}

/* SPR id, x, y */
static int nat_spr(bs_State* S) {
    int id = (int)bs_arg_number(S, 0);
    int x  = (int)bs_arg_number(S, 1);
    int y  = (int)bs_arg_number(S, 2);
    b2_spr(id, x, y);
    return 0;
}

/* TONE freq, dur, vol, channel */
static int nat_tone(bs_State* S) {
    b2_tone((int)bs_arg_number(S, 0),
            (int)bs_arg_number(S, 1),
            (int)bs_arg_number(S, 2),
            (int)bs_arg_number(S, 3));
    return 0;
}

/* ============================================================
 *  Регистрация
 * ============================================================ */
void b2_register_bs_api(bs_State* S) {
    bs_register_native(S, "CLS",     nat_cls);
    bs_register_native(S, "PSET",    nat_pset);
    bs_register_native(S, "PGET",    nat_pget);
    bs_register_native(S, "RECT",    nat_rect);
    bs_register_native(S, "OVAL",    nat_oval);
    bs_register_native(S, "LINE",    nat_line);
    bs_register_native(S, "PRINT",   nat_print);
    bs_register_native(S, "BTN",     nat_btn);
    bs_register_native(S, "BTNP",    nat_btnp);
    bs_register_native(S, "SPRDATA", nat_sprdata);
    bs_register_native(S, "SPR",     nat_spr);
    bs_register_native(S, "TONE",    nat_tone);
}