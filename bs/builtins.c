#include "bs_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifndef BS_PI
#define BS_PI 3.14159265358979323846
#endif

/* ============================================================
 * МАТЕМАТИКА
 * ============================================================ */

/* --- тригонометрия --- */
static int b_sin (bs_State* S) { bs_push_number(S, sin (bs_arg_number(S, 0))); return 1; }
static int b_cos (bs_State* S) { bs_push_number(S, cos (bs_arg_number(S, 0))); return 1; }
static int b_tan (bs_State* S) { bs_push_number(S, tan (bs_arg_number(S, 0))); return 1; }
static int b_asin(bs_State* S) { bs_push_number(S, asin(bs_arg_number(S, 0))); return 1; }
static int b_acos(bs_State* S) { bs_push_number(S, acos(bs_arg_number(S, 0))); return 1; }
static int b_atan(bs_State* S) { bs_push_number(S, atan(bs_arg_number(S, 0))); return 1; }
static int b_hsin(bs_State* S) { bs_push_number(S, sinh(bs_arg_number(S, 0))); return 1; }
static int b_hcos(bs_State* S) { bs_push_number(S, cosh(bs_arg_number(S, 0))); return 1; }
static int b_htan(bs_State* S) { bs_push_number(S, tanh(bs_arg_number(S, 0))); return 1; }

/* --- обычная математика --- */
static int b_abs(bs_State* S) {
    double x = bs_arg_number(S, 0);
    bs_push_number(S, x < 0 ? -x : x);
    return 1;
}
static int b_sgn(bs_State* S) {
    double x = bs_arg_number(S, 0);
    bs_push_number(S, x > 0 ? 1 : (x < 0 ? -1 : 0));
    return 1;
}
static int b_int(bs_State* S) { bs_push_number(S, floor(bs_arg_number(S, 0))); return 1; }
static int b_exp(bs_State* S) { bs_push_number(S, exp(bs_arg_number(S, 0)));   return 1; }
static int b_log(bs_State* S) { bs_push_number(S, log(bs_arg_number(S, 0)));   return 1; }
static int b_sqr(bs_State* S) { bs_push_number(S, sqrt(bs_arg_number(S, 0)));  return 1; }

static int b_min(bs_State* S) {
    double a = bs_arg_number(S, 0), b = bs_arg_number(S, 1);
    bs_push_number(S, a < b ? a : b);
    return 1;
}
static int b_max(bs_State* S) {
    double a = bs_arg_number(S, 0), b = bs_arg_number(S, 1);
    bs_push_number(S, a > b ? a : b);
    return 1;
}

/* --- случайные числа ---
     RND       → double [0, 1)
     RND(n)    → int   [0, n]    (n+1 значений, n >= 0)
     RND(<0)   → 0
*/
static int b_rnd(bs_State* S) {
    if (bs_argc(S) == 0) {
        double r = (double)rand() / ((double)RAND_MAX + 1.0);
        bs_push_number(S, r);
        return 1;
    }
    int n = (int)bs_arg_number(S, 0);
    if (n < 0) { bs_push_number(S, 0); return 1; }
    double r = (double)rand() / ((double)RAND_MAX + 1.0);
    int v = (int)(r * (n + 1));
    if (v > n) v = n;
    bs_push_number(S, (double)v);
    return 1;
}

static int b_randomize(bs_State* S) {
    if (bs_argc(S) == 0) srand((unsigned)time(NULL));
    else                 srand((unsigned)bs_arg_number(S, 0));
    return 0;
}

/* ============================================================
 * СТРОКИ
 * ============================================================ */

/* push подстроки произвольной длины без фиксированного буфера */
static void push_substr(bs_State* S, const char* src, int len) {
    if (len <= 0) { bs_push_string(S, ""); return; }
    char* buf = malloc((size_t)len + 1);
    if (!buf) { bs_push_string(S, ""); return; }
    memcpy(buf, src, (size_t)len);
    buf[len] = '\0';
    bs_push_string(S, buf);
    free(buf);
}

static int b_len(bs_State* S) {
    bs_push_number(S, (double)strlen(bs_arg_string(S, 0)));
    return 1;
}

/* LEFT$(s$, n): первые n символов */
static int b_left(bs_State* S) {
    const char* s = bs_arg_string(S, 0);
    int n   = (int)bs_arg_number(S, 1);
    int len = (int)strlen(s);
    if (n < 0)   n = 0;
    if (n > len) n = len;
    push_substr(S, s, n);
    return 1;
}

/* RIGHT$(s$, n): последние n символов */
static int b_right(bs_State* S) {
    const char* s = bs_arg_string(S, 0);
    int n   = (int)bs_arg_number(S, 1);
    int len = (int)strlen(s);
    if (n < 0)   n = 0;
    if (n > len) n = len;
    push_substr(S, s + (len - n), n);
    return 1;
}

/* MID$(s$, start[, len]): 1-based, len опционально */
static int b_mid(bs_State* S) {
    const char* s = bs_arg_string(S, 0);
    int start = (int)bs_arg_number(S, 1);   /* 1-based */
    int len   = (int)strlen(s);
    if (start < 1)   start = 1;
    if (start > len) { bs_push_string(S, ""); return 1; }
    int from  = start - 1;
    int avail = len - from;
    int n     = avail;
    if (bs_argc(S) >= 3) {
        int want = (int)bs_arg_number(S, 2);
        if (want < 0) want = 0;
        if (want < avail) n = want;
    }
    push_substr(S, s + from, n);
    return 1;
}

/* MID_SET(target, start[, len], repl) → новая строка.
   Statement-форма:  MID$(target$, start[, len]) = repl$
   Длина результата = длине target; лишние символы repl обрезаются,
   недостающие — сохраняются из target. start > LEN(target) — no-op. */
static int b_mid_set(bs_State* S) {
    int nargs = bs_argc(S);
    if (nargs < 3) { bs_push_string(S, bs_arg_string(S, 0)); return 1; }
    if (nargs > 4) nargs = 4;

    const char* target = bs_arg_string(S, 0);
    int start    = (int)bs_arg_number(S, 1);
    int has_len  = (nargs >= 4);
    int len_arg  = has_len ? (int)bs_arg_number(S, 2) : 0;
    const char* repl = bs_arg_string(S, has_len ? 3 : 2);

    int target_len = (int)strlen(target);
    int repl_len   = (int)strlen(repl);
    int start0     = start - 1;

    if (start0 < 0) start0 = 0;
    if (start0 >= target_len) {
        bs_push_string(S, target);     /* no-op */
        return 1;
    }

    int remaining = target_len - start0;
    int n_to_copy;
    if (has_len) {
        if (len_arg < 0) len_arg = 0;
        n_to_copy = len_arg < remaining ? len_arg : remaining;
    } else {
        n_to_copy = remaining;
    }

    int copy_len = repl_len < n_to_copy ? repl_len : n_to_copy;

    char* buf = malloc((size_t)target_len + 1);
    if (!buf) { bs_push_string(S, target); return 1; }
    memcpy(buf, target, (size_t)target_len);
    buf[target_len] = '\0';
    if (copy_len > 0)
        memcpy(buf + start0, repl, (size_t)copy_len);
    bs_push_string(S, buf);
    free(buf);
    return 1;
}

/* UBOUND(arr[, dim]) — верхняя граница массива (1-based dim).
   Аргумент arr приходит как BS_ARRAY (не разыменованный). */
static int b_ubound(bs_State* S) {
    bs_Value v = bs_arg(S, 0);
    if (v.type != BS_ARRAY || !v.as.arr) { bs_push_number(S, 0); return 1; }
    int dim = 1;
    if (bs_argc(S) >= 2) dim = (int)bs_arg_number(S, 1);
    if (dim < 1 || dim > v.as.arr->ndims) { bs_push_number(S, 0); return 1; }
    bs_push_number(S, (double)v.as.arr->dims[dim - 1]);
    return 1;
}

/* SWAP_ELEM(arrA, iA..., arrB, iB...) — обмен двух элементов массивов
   одинаковой размерности. Натив меняет элементы прямо в GC-объектах,
   на стек ничего не возвращает. */
static int b_swap_elem(bs_State* S) {
    int nargs = bs_argc(S);
    if (nargs < 4 || (nargs % 2) != 0) return 0;
    int ndims = nargs / 2 - 1;
    if (ndims < 1 || ndims > BS_MAX_ARRAY_DIMS) return 0;

    bs_Value avA = bs_arg(S, 0);
    bs_Value avB = bs_arg(S, ndims + 1);
    if (avA.type != BS_ARRAY || avB.type != BS_ARRAY) return 0;
    bs_Array* arrA = avA.as.arr;
    bs_Array* arrB = avB.as.arr;
    if (!arrA || !arrB) return 0;
    if (arrA->ndims != ndims || arrB->ndims != ndims) return 0;

    int offA = 0, mult = 1;
    for (int i = ndims - 1; i >= 0; i--) {
        int idx = (int)v_numof(bs_arg(S, 1 + i));
        int sz = arrA->dims[i] + 1;
        if (idx < 0 || idx >= sz) return 0;
        offA += idx * mult;
        mult *= sz;
    }
    int offB = 0; mult = 1;
    for (int i = ndims - 1; i >= 0; i--) {
        int idx = (int)v_numof(bs_arg(S, ndims + 2 + i));
        int sz = arrB->dims[i] + 1;
        if (idx < 0 || idx >= sz) return 0;
        offB += idx * mult;
        mult *= sz;
    }

    bs_Value tmp = arrA->data[offA];
    arrA->data[offA] = arrB->data[offB];
    arrB->data[offB] = tmp;
    return 0;
}

/* CHR$(n): байт n (маска 0xFF — как в классических BASIC) */
static int b_chr(bs_State* S) {
    int n = (int)bs_arg_number(S, 0) & 0xFF;
    char buf[2] = { (char)n, 0 };
    bs_push_string(S, buf);
    return 1;
}

/* ASC(s$): код первого символа, 0 для пустой строки */
static int b_asc(bs_State* S) {
    const char* s = bs_arg_string(S, 0);
    bs_push_number(S, s[0] ? (double)(unsigned char)s[0] : 0.0);
    return 1;
}

/* STR$(n): число в строку, без ведущего пробела, формат %.14g (как PRINT) */
static int b_str(bs_State* S) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.14g", bs_arg_number(S, 0));
    bs_push_string(S, buf);
    return 1;
}

/* VAL(s$): строка в число, 0 при ошибке (strtod + проверка "не распарсилось ни байта") */
static int b_val(bs_State* S) {
    const char* s = bs_arg_string(S, 0);
    char* end = NULL;
    double x = strtod(s, &end);
    if (end == s) x = 0.0;
    bs_push_number(S, x);
    return 1;
}

/* ============================================================
 * РЕГИСТРАЦИЯ
 * ============================================================ */

void bs_register_builtins_math(bs_State* S) {
    srand(0);

    bs_register_native(S, "SIN",       b_sin);
    bs_register_native(S, "COS",       b_cos);
    bs_register_native(S, "TAN",       b_tan);
    bs_register_native(S, "ASIN",      b_asin);
    bs_register_native(S, "ACOS",      b_acos);
    bs_register_native(S, "ATAN",      b_atan);
    bs_register_native(S, "HSIN",      b_hsin);
    bs_register_native(S, "HCOS",      b_hcos);
    bs_register_native(S, "HTAN",      b_htan);
    bs_register_native(S, "ABS",       b_abs);
    bs_register_native(S, "SGN",       b_sgn);
    bs_register_native(S, "INT",       b_int);
    bs_register_native(S, "EXP",       b_exp);
    bs_register_native(S, "LOG",       b_log);
    bs_register_native(S, "SQR",       b_sqr);
    bs_register_native(S, "MIN",       b_min);
    bs_register_native(S, "MAX",       b_max);
    bs_register_native(S, "RND",       b_rnd);
    bs_register_native(S, "RANDOMIZE", b_randomize);
}

void bs_register_builtins_string(bs_State* S) {
    bs_register_native(S, "LEN",       b_len);
    bs_register_native(S, "LEFT$",     b_left);
    bs_register_native(S, "RIGHT$",    b_right);
    bs_register_native(S, "MID$",      b_mid);
    bs_register_native(S, "CHR$",      b_chr);
    bs_register_native(S, "ASC",       b_asc);
    bs_register_native(S, "STR$",      b_str);
    bs_register_native(S, "VAL",       b_val);
    bs_register_native(S, "MID_SET",   b_mid_set);   /* внутренний, для statement-формы MID$ */
    bs_register_native(S, "UBOUND",    b_ubound);
    bs_register_native(S, "SWAP_ELEM", b_swap_elem);
}