#include "bs_internal.h"
#include "lexer.h"
#include "preproc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BS_MAX_LOOP_DEPTH     16
#define BS_MAX_EXITS_PER_LOOP 64

typedef struct {
    int kind;        /* 0 = scalar, 1 = array element */
    int slot;
    int is_global;   /* 1 = slot в S->globals[], 0 = slot в текущем chunk */
    int ndims;       /* 0 = неизвестно (param-массив), >0 = точное */
} LValue;

typedef struct {
    int patches[BS_MAX_EXITS_PER_LOOP];
    int count;
} ExitList;

typedef struct {
    int  patch_pos;
    int  line;
    char label[256];
} PendingPatch;

typedef struct {
    Lexer     L;
    bs_State* S;
    bs_Chunk* chunk;
    int       failed;
    int       in_sub;
    ExitList  exit_stack[BS_MAX_LOOP_DEPTH];
    int       loop_depth;
    PendingPatch* pending;
    int           npending, pcap;
    int*          pending_exits;
    int           npending_exits, cappending_exits;
} Parser;

/* forward declarations */
static void parse_expr(Parser* P);
static void parse_stmt(Parser* P);
static int  parse_ident_or_call(Parser* P, int allow_statement_form);
static int  parse_call_args(Parser* P);
static int  parse_print_args(Parser* P);

static void perr(Parser* P, const char* msg) {
    if (P->failed) return;
    P->failed = 1;
    snprintf(P->S->error, sizeof(P->S->error), "%s", msg);
    P->S->error_line = P->L.cur.line;
}

/* ============================================================
 * Разрешение имён: локальный → GLOBAL → новый локальный
 * ============================================================ */
static void emit_load(Parser* P, const char* name) {
    int slot = chunk_lookup_global(P->chunk, name);
    if (slot >= 0) { chunk_emit(P->chunk, OP_LOAD, slot, 0); return; }
    if (P->in_sub) {
        int g = chunk_find_global_in_main(P->S, name);
        if (g >= 0) { chunk_emit(P->chunk, OP_LOAD_G, g, 0); return; }
    }
    slot = chunk_find_global(P->chunk, name);
    if (slot < 0) { perr(P, "too many vars"); return; }
    chunk_emit(P->chunk, OP_LOAD, slot, 0);
}

static void emit_store(Parser* P, const char* name) {
    int slot = chunk_lookup_global(P->chunk, name);
    if (slot >= 0) { chunk_emit(P->chunk, OP_STORE, slot, 0); return; }
    if (P->in_sub) {
        int g = chunk_find_global_in_main(P->S, name);
        if (g >= 0) { chunk_emit(P->chunk, OP_STORE_G, g, 0); return; }
    }
    slot = chunk_find_global(P->chunk, name);
    if (slot < 0) { perr(P, "too many vars"); return; }
    chunk_emit(P->chunk, OP_STORE, slot, 0);
}

typedef struct { bs_ArrayDecl* ad; int is_global; } ArrayRef;

static ArrayRef find_array_ref(Parser* P, const char* name) {
    bs_ArrayDecl* ad = chunk_find_array_decl(P->chunk, name);
    if (ad) return (ArrayRef){ ad, 0 };
    if (P->in_sub) {
        bs_ArrayDecl* mad = chunk_find_array_decl(&P->S->chunk, name);
        if (mad && P->S->is_global[mad->slot]) return (ArrayRef){ mad, 1 };
    }
    return (ArrayRef){ NULL, 0 };
}

/* ============================================================
 * EXIT (циклы)
 * ============================================================ */
static void emit_exit(Parser* P) {
    if (P->loop_depth <= 0) { perr(P, "EXIT outside of loop"); return; }
    ExitList* el = &P->exit_stack[P->loop_depth - 1];
    if (el->count >= BS_MAX_EXITS_PER_LOOP) {
        perr(P, "too many EXIT statements in loop");
        return;
    }
    int pos = chunk_emit(P->chunk, OP_JUMP, 0, 0);
    el->patches[el->count++] = pos;
}

static void patch_exits(Parser* P, int target) {
    ExitList* el = &P->exit_stack[P->loop_depth - 1];
    for (int i = 0; i < el->count; i++)
        P->chunk->code[el->patches[i]].a = target;
}

/* ============================================================
 * EXIT SUB
 * ============================================================ */
static void emit_exit_sub(Parser* P) {
    if (!P->in_sub) { perr(P, "EXIT SUB outside SUB"); return; }
    if (P->npending_exits >= P->cappending_exits) {
        P->cappending_exits = P->cappending_exits ? P->cappending_exits * 2 : 8;
        P->pending_exits = realloc(P->pending_exits,
                                   P->cappending_exits * sizeof(int));
    }
    int pos = chunk_emit(P->chunk, OP_JUMP, 0, 0);
    P->pending_exits[P->npending_exits++] = pos;
}

/* ============================================================
 * Отложенные переходы (GOTO/GOSUB по меткам)
 * ============================================================ */
static void add_pending(Parser* P, int patch_pos, int line, const char* label) {
    if (P->npending >= P->pcap) {
        P->pcap = P->pcap ? P->pcap * 2 : 16;
        P->pending = realloc(P->pending, P->pcap * sizeof(PendingPatch));
    }
    P->pending[P->npending].patch_pos = patch_pos;
    P->pending[P->npending].line = line;
    strncpy(P->pending[P->npending].label, label, 255);
    P->pending[P->npending].label[255] = 0;
    P->npending++;
}

/* ============================================================
 * Идентификатор в разных ролях
 * ============================================================ */
static int parse_ident_or_call(Parser* P, int allow_statement_form) {
    Lexer* L = &P->L;

    if (L->cur.type != T_IDENT) { perr(P, "identifier expected"); return -1; }

    char name[256];
    strcpy(name, L->cur.text);
    lex_advance(L);

    ArrayRef ar = find_array_ref(P, name);
    bs_ArrayDecl* ad = ar.ad;
    int idx = chunk_find_native(P->S, name);
    int is_native = (idx >= 0);

    /* 1. Индексация массива: NAME(i[,j[,k]]) */
    if (ad && L->cur.type == T_LPAREN) {
        lex_advance(L);
        if (ar.is_global) chunk_emit(P->chunk, OP_LOAD_G, ad->slot, 0);
        else              chunk_emit(P->chunk, OP_LOAD,   ad->slot, 0);

        int n = 0;
        parse_expr(P); n++;
        while (L->cur.type == T_COMMA) {
            lex_advance(L);
            parse_expr(P); n++;
        }
        if (L->cur.type != T_RPAREN) { perr(P, "expected ')'"); return -1; }
        lex_advance(L);

        if (ad->ndims > 0 && n != ad->ndims) {
            perr(P, "wrong number of array indices");
            return -1;
        }

        if (allow_statement_form) {
            if (L->cur.type != T_EQ) {
                perr(P, "array element as statement requires assignment");
                return -1;
            }
            lex_advance(L);
            parse_expr(P);
            chunk_emit(P->chunk, OP_INDEX_SET, n, 0);
            return 1;
        }
        chunk_emit(P->chunk, OP_INDEX_GET, n, 0);
        return 0;
    }

    /* 2. Присваивание скаляру */
    if (allow_statement_form && L->cur.type == T_EQ) {
        if (ad) { perr(P, "cannot assign whole array"); return -1; }
        lex_advance(L);
        parse_expr(P);
        emit_store(P, name);
        chunk_emit(P->chunk, OP_POP, 0, 0);
        return 1;
    }

    /* 3. MID$(var$, start[, len]) = repl$ — statement form */
    if (allow_statement_form && is_native && !strcmp(name, "MID$")
        && L->cur.type == T_LPAREN) {
        int set_idx = chunk_find_native(P->S, "MID_SET");
        if (set_idx < 0) { perr(P, "MID_SET not registered"); return -1; }

        lex_advance(L);
        if (L->cur.type != T_IDENT) { perr(P, "MID$: variable expected"); return -1; }
        char target[256];
        strcpy(target, L->cur.text);
        lex_advance(L);

        int tslot, t_is_global = 0;
        if (P->in_sub) {
            int g = chunk_find_global_in_main(P->S, target);
            if (g >= 0 && chunk_lookup_global(P->chunk, target) < 0) {
                tslot = g; t_is_global = 1;
            } else {
                tslot = chunk_find_global(P->chunk, target);
            }
        } else {
            tslot = chunk_find_global(P->chunk, target);
        }
        if (tslot < 0) { perr(P, "too many globals"); return -1; }

        if (L->cur.type != T_COMMA) { perr(P, "MID$: ',' expected"); return -1; }
        lex_advance(L);

        if (t_is_global) chunk_emit(P->chunk, OP_LOAD_G, tslot, 0);
        else             chunk_emit(P->chunk, OP_LOAD,   tslot, 0);
        parse_expr(P);

        int has_len = 0;
        if (L->cur.type == T_COMMA) {
            lex_advance(L);
            parse_expr(P);
            has_len = 1;
        }
        if (L->cur.type != T_RPAREN) { perr(P, "MID$: ')' expected"); return -1; }
        lex_advance(L);
        if (L->cur.type != T_EQ) { perr(P, "MID$: '=' expected"); return -1; }
        lex_advance(L);
        parse_expr(P);

        int nargs = has_len ? 4 : 3;
        chunk_emit(P->chunk, OP_CALL_NATIVE, set_idx, nargs);
        if (t_is_global) chunk_emit(P->chunk, OP_STORE_G, tslot, 0);
        else             chunk_emit(P->chunk, OP_STORE,   tslot, 0);
        chunk_emit(P->chunk, OP_POP, 0, 0);
        return 1;
    }

    /* 4. UBOUND(name[, dim]) */
    if (!allow_statement_form && is_native && !strcmp(name, "UBOUND")
        && L->cur.type == T_LPAREN) {
        lex_advance(L);
        if (L->cur.type != T_IDENT) { perr(P, "UBOUND: array name expected"); return -1; }
        ArrayRef ub = find_array_ref(P, L->cur.text);
        if (!ub.ad) { perr(P, "UBOUND: unknown array"); return -1; }
        lex_advance(L);
        if (ub.is_global) chunk_emit(P->chunk, OP_LOAD_G, ub.ad->slot, 0);
        else              chunk_emit(P->chunk, OP_LOAD,   ub.ad->slot, 0);
        int nargs = 1;
        if (L->cur.type == T_COMMA) {
            lex_advance(L);
            parse_expr(P);
            nargs = 2;
        }
        if (L->cur.type != T_RPAREN) { perr(P, "UBOUND: ')' expected"); return -1; }
        lex_advance(L);
        chunk_emit(P->chunk, OP_CALL_NATIVE, idx, nargs);
        return 0;
    }

    /* 5. Функция в выражении: NAME или NAME(args) */
    if (!allow_statement_form && is_native
        && chunk_lookup_global(P->chunk, name) < 0
        && !(P->in_sub && chunk_find_global_in_main(P->S, name) >= 0)) {
        int nargs = 0;
        if (L->cur.type == T_LPAREN) {
            lex_advance(L);
            if (L->cur.type != T_RPAREN) {
                parse_expr(P); nargs++;
                while (L->cur.type == T_COMMA) {
                    lex_advance(L);
                    parse_expr(P); nargs++;
                }
            }
            if (L->cur.type != T_RPAREN) { perr(P, "expected ')'"); return -1; }
            lex_advance(L);
        }
        chunk_emit(P->chunk, OP_CALL_NATIVE, idx, nargs);
        return 0;
    }

    /* 6. Команда-оператор без скобок: NAME arg, arg */
    if (allow_statement_form && is_native) {
        int nargs;
        if (!strcmp(name, "PRINT")) {
            /* PRINT — особый: передаём маску разделителей последним аргументом */
            nargs = parse_print_args(P);
        } else {
            nargs = parse_call_args(P);
        }
        chunk_emit(P->chunk, OP_CALL_NATIVE, idx, nargs);
        chunk_emit(P->chunk, OP_POP, 0, 0);
        return 1;
    }

    /* 7. Переменная в выражении (в т.ч. bare-array — для CALL/выражений) */
    if (allow_statement_form) { perr(P, "unknown command"); return -1; }
    if (ad) {
        if (ar.is_global) chunk_emit(P->chunk, OP_LOAD_G, ad->slot, 0);
        else              chunk_emit(P->chunk, OP_LOAD,   ad->slot, 0);
        return 0;
    }
    emit_load(P, name);
    return 0;
}

/* ============================================================
 * Выражения
 * ============================================================ */
static void parse_primary(Parser* P) {
    Lexer* L = &P->L;
    switch (L->cur.type) {
        case T_NUMBER:
            chunk_emit(P->chunk, OP_CONST, chunk_add_const(P->chunk, v_num(L->cur.num)), 0);
            lex_advance(L);
            break;
        case T_STRING:
        case T_LONGSTRING:
            chunk_emit(P->chunk, OP_CONST,
                       chunk_add_const(P->chunk, v_str(str_take(P->S, L->cur.text))), 0);
            lex_advance(L);
            break;
        case T_LPAREN:
            lex_advance(L); parse_expr(P);
            if (L->cur.type != T_RPAREN) { perr(P, "expected ')'"); return; }
            lex_advance(L);
            break;
        case T_MINUS:
            lex_advance(L); parse_primary(P); chunk_emit(P->chunk, OP_NEG, 0, 0);
            break;
        case T_NOT:
            lex_advance(L); parse_primary(P); chunk_emit(P->chunk, OP_NOT, 0, 0);
            break;
        case T_PI:
            chunk_emit(P->chunk, OP_CONST,
                       chunk_add_const(P->chunk, v_num(3.14159265358979323846)), 0);
            lex_advance(L);
            break;
        case T_IDENT:
            (void)parse_ident_or_call(P, 0);
            break;
        default:
            perr(P, "expression expected");
    }
}

static void parse_factor(Parser* P) {
    parse_primary(P);
    for (;;) {
        TokType t = P->L.cur.type;
        if (t == T_STAR || t == T_SLASH || t == T_BSLASH || t == T_MOD || t == T_CARET) {
            lex_advance(&P->L); parse_primary(P);
            uint8_t op = t == T_STAR   ? OP_MUL :
                         t == T_SLASH  ? OP_DIV :
                         t == T_BSLASH ? OP_IDIV :
                         t == T_MOD    ? OP_MOD : OP_POW;
            chunk_emit(P->chunk, op, 0, 0);
        } else break;
    }
}

static void parse_term(Parser* P) {
    parse_factor(P);
    for (;;) {
        TokType t = P->L.cur.type;
        if (t == T_PLUS || t == T_MINUS) {
            lex_advance(&P->L); parse_factor(P);
            chunk_emit(P->chunk, t == T_PLUS ? OP_ADD : OP_SUB, 0, 0);
        } else break;
    }
}

static void parse_cmp(Parser* P) {
    parse_term(P);
    for (;;) {
        TokType t = P->L.cur.type;
        if (t == T_EQ || t == T_NE || t == T_LT || t == T_LE || t == T_GT || t == T_GE) {
            lex_advance(&P->L); parse_term(P);
            uint8_t op = t == T_EQ ? OP_EQ : t == T_NE ? OP_NE :
                         t == T_LT ? OP_LT : t == T_LE ? OP_LE :
                         t == T_GT ? OP_GT : OP_GE;
            chunk_emit(P->chunk, op, 0, 0);
        } else break;
    }
}

static void parse_expr(Parser* P) {
    parse_cmp(P);
    for (;;) {
        TokType t = P->L.cur.type;
        if (t == T_AND || t == T_OR || t == T_XOR) {
            lex_advance(&P->L); parse_cmp(P);
            chunk_emit(P->chunk, t == T_AND ? OP_AND : t == T_OR ? OP_OR : OP_XOR, 0, 0);
        } else break;
    }
}

/* Разбор аргументов PRINT.
   Возвращает общее число аргументов на стеке = nargs_values + 1 (маска).
   Маска — строка из ',' и ';', по одной на каждый разделитель. */
static int parse_print_args(Parser* P) {
    char mask[256];
    int nmask = 0;
    int nvals = 0;

    if (P->L.cur.type != T_NEWLINE && P->L.cur.type != T_EOF) {
        parse_expr(P); nvals++;
        while (P->L.cur.type == T_COMMA || P->L.cur.type == T_SEMI) {
            char sep = (P->L.cur.type == T_COMMA) ? ',' : ';';
            if (nmask < 255) mask[nmask++] = sep;
            lex_advance(&P->L);
            if (P->L.cur.type == T_NEWLINE || P->L.cur.type == T_EOF) break;
            parse_expr(P); nvals++;
        }
    }
    mask[nmask] = 0;

    int const_idx = chunk_add_const(P->chunk, v_str(str_take(P->S, mask)));
    chunk_emit(P->chunk, OP_CONST, const_idx, 0);

    return nvals + 1;   /* +1 за маску */
}

static int parse_call_args(Parser* P) {
    int n = 0;
    if (P->L.cur.type == T_NEWLINE || P->L.cur.type == T_EOF) return 0;
    parse_expr(P); n++;
    while (P->L.cur.type == T_COMMA || P->L.cur.type == T_SEMI) {
        lex_advance(&P->L);
        if (P->L.cur.type == T_NEWLINE || P->L.cur.type == T_EOF) break;
        parse_expr(P); n++;
    }
    return n;
}

/* ============================================================
 * lvalue для SWAP
 * ============================================================ */
static int parse_lvalue(Parser* P, LValue* out) {
    Lexer* L = &P->L;
    if (L->cur.type != T_IDENT) { perr(P, "lvalue: identifier expected"); return -1; }

    char name[256];
    strcpy(name, L->cur.text);
    lex_advance(L);

    ArrayRef ar = find_array_ref(P, name);

    if (ar.ad && L->cur.type == T_LPAREN) {
        lex_advance(L);
        if (ar.is_global) chunk_emit(P->chunk, OP_LOAD_G, ar.ad->slot, 0);
        else              chunk_emit(P->chunk, OP_LOAD,   ar.ad->slot, 0);

        int n = 0;
        parse_expr(P); n++;
        while (L->cur.type == T_COMMA) {
            lex_advance(L);
            parse_expr(P); n++;
        }
        if (L->cur.type != T_RPAREN) { perr(P, "expected ')'"); return -1; }
        lex_advance(L);
        if (ar.ad->ndims > 0 && n != ar.ad->ndims) {
            perr(P, "wrong number of array indices");
            return -1;
        }
        out->kind = 1;
        out->slot = ar.ad->slot;
        out->is_global = ar.is_global;
        out->ndims = n;
        return 0;
    }

    if (ar.ad) { perr(P, "array requires index"); return -1; }

    int slot = chunk_lookup_global(P->chunk, name);
    int is_global = 0;
    if (slot < 0) {
        if (P->in_sub) {
            int g = chunk_find_global_in_main(P->S, name);
            if (g >= 0) { slot = g; is_global = 1; }
        }
        if (slot < 0) {
            slot = chunk_find_global(P->chunk, name);
            if (slot < 0) { perr(P, "too many globals"); return -1; }
        }
    }
    out->kind = 0;
    out->slot = slot;
    out->is_global = is_global;
    out->ndims = 0;
    return 0;
}

/* ============================================================
 * Пропуск тела SUB (при компиляции main)
 * ============================================================ */
static void skip_sub(Parser* P) {
    Lexer* L = &P->L;
    /* L->cur == T_SUB */
    lex_advance(L);
    if (L->cur.type == T_IDENT) lex_advance(L);
    if (L->cur.type == T_LPAREN) {
        lex_advance(L);
        int d = 1;
        while (d > 0 && L->cur.type != T_EOF) {
            if (L->cur.type == T_LPAREN) d++;
            else if (L->cur.type == T_RPAREN) d--;
            lex_advance(L);
        }
    }
    while (L->cur.type != T_EOF) {
        if (L->cur.type == T_END && lex_peek(L) == T_SUB) {
            lex_advance(L);  /* END */
            lex_advance(L);  /* SUB */
            return;
        }
        lex_advance(L);
    }
    perr(P, "unterminated SUB");
}

/* ============================================================
 * Оператор
 * ============================================================ */
static void parse_stmt(Parser* P) {
    Lexer* L = &P->L;

    if (L->cur.type == T_NEWLINE) { lex_advance(L); return; }
    if (L->cur.type == T_COLON)   { lex_advance(L); return; }
    if (L->cur.type == T_EOF)     return;

    /* END SUB — оставляем вызывающему коду */
    if (L->cur.type == T_END && lex_peek(L) == T_SUB) {
        if (!P->in_sub) { perr(P, "END SUB without SUB"); }
        return;
    }

    /* SUB — в main пропускаем, в sub — ошибка */
    if (L->cur.type == T_SUB) {
        if (P->in_sub) { perr(P, "nested SUB not allowed"); return; }
        skip_sub(P);
        return;
    }

    /* GLOBAL var-list */
    if (L->cur.type == T_GLOBAL) {
        if (P->in_sub) { perr(P, "GLOBAL inside SUB"); return; }
        lex_advance(L);
        while (L->cur.type == T_IDENT) {
            int slot = chunk_find_global(P->chunk, L->cur.text);
            if (slot < 0) { perr(P, "too many globals"); return; }
            P->S->is_global[slot] = 1;
            lex_advance(L);
            if (L->cur.type == T_COMMA) { lex_advance(L); continue; }
            break;
        }
        return;
    }

    /* ---- INPUT ["prompt";]var | INPUT ["prompt";]arr(indices) ---- */
    if (L->cur.type == T_INPUT) {
        lex_advance(L);

        /* Опциональный prompt — только строковый литерал */
        char prompt_buf[1024] = "";
        if (L->cur.type == T_STRING) {
            strcpy(prompt_buf, L->cur.text);
            lex_advance(L);
            if (L->cur.type != T_SEMI) {
                perr(P, "INPUT: ';' expected after prompt");
                return;
            }
            lex_advance(L);
        }

        /* Получатель */
        if (L->cur.type != T_IDENT) {
            perr(P, "INPUT: variable expected");
            return;
        }
        char varname[256];
        strcpy(varname, L->cur.text);
        lex_advance(L);

        int input_idx = chunk_find_native(P->S, "INPUT");
        if (input_idx < 0) { perr(P, "INPUT not registered"); return; }
        int val_idx = chunk_find_native(P->S, "VAL");

        ArrayRef ar_in = find_array_ref(P, varname);

        /* Ветка 1: элемент массива */
        if (ar_in.ad && L->cur.type == T_LPAREN) {
            lex_advance(L);
            if (ar_in.is_global) chunk_emit(P->chunk, OP_LOAD_G, ar_in.ad->slot, 0);
            else                 chunk_emit(P->chunk, OP_LOAD,   ar_in.ad->slot, 0);

            int nidx = 0;
            parse_expr(P); nidx++;
            while (L->cur.type == T_COMMA) {
                lex_advance(L);
                parse_expr(P); nidx++;
            }
            if (L->cur.type != T_RPAREN) { perr(P, "INPUT: ')' expected"); return; }
            lex_advance(L);

            if (ar_in.ad->ndims > 0 && nidx != ar_in.ad->ndims) {
                perr(P, "INPUT: wrong number of array indices");
                return;
            }

            int const_idx = chunk_add_const(P->chunk, v_str(str_take(P->S, prompt_buf)));
            chunk_emit(P->chunk, OP_CONST, const_idx, 0);
            chunk_emit(P->chunk, OP_CALL_NATIVE, input_idx, 1);

            int vnlen = (int)strlen(varname);
            int array_is_string = (vnlen > 0 && varname[vnlen - 1] == '$');
            if (!array_is_string && val_idx >= 0) {
                chunk_emit(P->chunk, OP_CALL_NATIVE, val_idx, 1);
            }

            chunk_emit(P->chunk, OP_INDEX_SET, nidx, 0);
            return;
        }

        if (ar_in.ad) { perr(P, "INPUT: array requires index"); return; }
        if (L->cur.type == T_LPAREN) { perr(P, "INPUT: array not declared"); return; }

        /* Ветка 2: скаляр */
        int nlen = (int)strlen(varname);
        int is_string = (nlen > 0 && varname[nlen - 1] == '$');

        int slot = -1, slot_is_global = 0;
        if (P->in_sub) {
            int g = chunk_find_global_in_main(P->S, varname);
            if (g >= 0 && chunk_lookup_global(P->chunk, varname) < 0) {
                slot = g; slot_is_global = 1;
            }
        }
        if (slot < 0) slot = chunk_find_global(P->chunk, varname);
        if (slot < 0) { perr(P, "too many globals"); return; }

        int const_idx = chunk_add_const(P->chunk, v_str(str_take(P->S, prompt_buf)));
        chunk_emit(P->chunk, OP_CONST, const_idx, 0);
        chunk_emit(P->chunk, OP_CALL_NATIVE, input_idx, 1);

        if (!is_string && val_idx >= 0) {
            chunk_emit(P->chunk, OP_CALL_NATIVE, val_idx, 1);
        }

        if (slot_is_global) chunk_emit(P->chunk, OP_STORE_G, slot, 0);
        else                chunk_emit(P->chunk, OP_STORE,   slot, 0);
        chunk_emit(P->chunk, OP_POP, 0, 0);
        return;
    }

    if (L->cur.type == T_LET) lex_advance(L);

    if (L->cur.type == T_IDENT) {
        (void)parse_ident_or_call(P, 1);
        return;
    }

    /* CALL name[(args)] */
    if (L->cur.type == T_CALL) {
        lex_advance(L);
        if (L->cur.type != T_IDENT) { perr(P, "CALL: name expected"); return; }
        int sub_idx = -1;
        for (int i = 0; i < P->S->n_subs_registered; i++)
            if (!strcmp(P->S->sub_table[i].name, L->cur.text)) { sub_idx = i; break; }
        if (sub_idx < 0) { perr(P, "CALL: unknown SUB"); return; }
        lex_advance(L);

        int nargs = 0;
        if (L->cur.type == T_LPAREN) {
            lex_advance(L);
            if (L->cur.type != T_RPAREN) {
                parse_expr(P); nargs++;
                while (L->cur.type == T_COMMA) {
                    lex_advance(L);
                    parse_expr(P); nargs++;
                }
            }
            if (L->cur.type != T_RPAREN) { perr(P, "CALL: ')' expected"); return; }
            lex_advance(L);
        }
        if (nargs != P->S->sub_table[sub_idx].nparams) {
            perr(P, "CALL: argument count mismatch");
            return;
        }
        chunk_emit(P->chunk, OP_CALL_USER, sub_idx, nargs);
        chunk_emit(P->chunk, OP_POP, 0, 0);
        return;
    }

    /* Метка */
    if (L->cur.type == T_LABEL) {
        int rc = chunk_add_label(P->chunk, L->cur.text, P->chunk->ncode);
        if (rc == -2) { perr(P, "duplicate label"); return; }
        if (rc < 0)   { perr(P, "too many labels"); return; }
        lex_advance(L);
        return;
    }

    if (L->cur.type == T_GOTO) {
        lex_advance(L);
        if (L->cur.type != T_IDENT) { perr(P, "GOTO: label expected"); return; }
        int pos = chunk_emit(P->chunk, OP_JUMP, -1, 0);
        add_pending(P, pos, L->cur.line, L->cur.text);
        lex_advance(L);
        return;
    }

    if (L->cur.type == T_GOSUB) {
        lex_advance(L);
        if (L->cur.type != T_IDENT) { perr(P, "GOSUB: label expected"); return; }
        int pos = chunk_emit(P->chunk, OP_GOSUB, -1, 0);
        add_pending(P, pos, L->cur.line, L->cur.text);
        lex_advance(L);
        return;
    }

    if (L->cur.type == T_RETURN) {
        chunk_emit(P->chunk, OP_RETURN, 0, 0);
        lex_advance(L);
        return;
    }

    /* DIM [GLOBAL] name(dim1[, dim2[, dim3]]) */
    if (L->cur.type == T_DIM) {
        lex_advance(L);
        int is_global_dim = 0;
        if (L->cur.type == T_GLOBAL) {
            if (P->in_sub) { perr(P, "DIM GLOBAL inside SUB"); return; }
            is_global_dim = 1;
            lex_advance(L);
        }
        if (L->cur.type != T_IDENT) { perr(P, "DIM: name expected"); return; }
        char name[256];
        strcpy(name, L->cur.text);
        lex_advance(L);
        if (L->cur.type != T_LPAREN) { perr(P, "DIM: '(' expected"); return; }
        lex_advance(L);
        int ndims = 0;
        parse_expr(P); ndims++;
        while (L->cur.type == T_COMMA) {
            lex_advance(L);
            if (ndims >= BS_MAX_ARRAY_DIMS) {
                perr(P, "DIM: too many dimensions (max 3)");
                return;
            }
            parse_expr(P); ndims++;
        }
        if (L->cur.type != T_RPAREN) { perr(P, "DIM: ')' expected"); return; }
        lex_advance(L);

        int slot = chunk_find_global(P->chunk, name);
        if (slot < 0) { perr(P, "too many globals"); return; }
        if (is_global_dim) P->S->is_global[slot] = 1;

        int rc = chunk_add_array_decl(P->chunk, name, slot, ndims);
        if (rc == -2) { perr(P, "DIM: array already declared"); return; }
        if (rc < 0)   { perr(P, "DIM: too many arrays"); return; }

        chunk_emit(P->chunk, OP_NEW_ARRAY, slot, ndims);
        return;
    }

    /* SWAP a, b | SWAP A(i), B(j) */
    if (L->cur.type == T_SWAP) {
        lex_advance(L);
        LValue va, vb;
        if (parse_lvalue(P, &va) < 0) return;
        if (L->cur.type != T_COMMA) { perr(P, "SWAP: ',' expected"); return; }
        lex_advance(L);
        if (parse_lvalue(P, &vb) < 0) return;

        if (va.kind == 0 && vb.kind == 0) {
            if (va.is_global) chunk_emit(P->chunk, OP_LOAD_G, va.slot, 0);
            else              chunk_emit(P->chunk, OP_LOAD,   va.slot, 0);
            if (vb.is_global) chunk_emit(P->chunk, OP_LOAD_G, vb.slot, 0);
            else              chunk_emit(P->chunk, OP_LOAD,   vb.slot, 0);
            if (va.is_global) chunk_emit(P->chunk, OP_STORE_G, va.slot, 0);
            else              chunk_emit(P->chunk, OP_STORE,   va.slot, 0);
            chunk_emit(P->chunk, OP_POP, 0, 0);
            if (vb.is_global) chunk_emit(P->chunk, OP_STORE_G, vb.slot, 0);
            else              chunk_emit(P->chunk, OP_STORE,   vb.slot, 0);
            chunk_emit(P->chunk, OP_POP, 0, 0);
        } else if (va.kind == 1 && vb.kind == 1 && va.ndims == vb.ndims) {
            int swap_idx = chunk_find_native(P->S, "SWAP_ELEM");
            if (swap_idx < 0) { perr(P, "SWAP_ELEM not registered"); return; }
            int nargs = 2 * (va.ndims + 1);
            chunk_emit(P->chunk, OP_CALL_NATIVE, swap_idx, nargs);
        } else {
            perr(P, "SWAP: operands must be both scalars or both array elements of same dimension");
            return;
        }
        return;
    }

    /* IF */
    if (L->cur.type == T_IF) {
        lex_advance(L);
        parse_expr(P);
        if (L->cur.type != T_THEN) { perr(P, "expected THEN"); return; }
        lex_advance(L);

        if (L->cur.type == T_NEWLINE) {
            int jf = chunk_emit(P->chunk, OP_JUMP_IF_FALSE, 0, 0);
            lex_advance(L);
            while (L->cur.type != T_EOF && L->cur.type != T_ELSE &&
                   L->cur.type != T_END && !P->failed)
                parse_stmt(P);
            int jend = -1;
            if (L->cur.type == T_ELSE) {
                jend = chunk_emit(P->chunk, OP_JUMP, 0, 0);
                P->chunk->code[jf].a = P->chunk->ncode;
                lex_advance(L);
                while (L->cur.type != T_EOF && L->cur.type != T_END && !P->failed)
                    parse_stmt(P);
            }
            if (L->cur.type != T_END) { perr(P, "expected END IF"); return; }
            lex_advance(L);
            if (L->cur.type != T_IF) { perr(P, "expected END IF"); return; }
            lex_advance(L);
            if (jend < 0) P->chunk->code[jf].a = P->chunk->ncode;
            else          P->chunk->code[jend].a = P->chunk->ncode;
        } else {
            int jf = chunk_emit(P->chunk, OP_JUMP_IF_FALSE, 0, 0);
            parse_stmt(P);
            if (L->cur.type == T_ELSE) {
                int jend = chunk_emit(P->chunk, OP_JUMP, 0, 0);
                P->chunk->code[jf].a = P->chunk->ncode;
                lex_advance(L);
                parse_stmt(P);
                P->chunk->code[jend].a = P->chunk->ncode;
            } else {
                P->chunk->code[jf].a = P->chunk->ncode;
            }
        }
        return;
    }

    /* WHILE ... WEND */
    if (L->cur.type == T_WHILE) {
        lex_advance(L);
        int top = P->chunk->ncode;
        parse_expr(P);
        int jf = chunk_emit(P->chunk, OP_JUMP_IF_FALSE, 0, 0);
        if (P->loop_depth >= BS_MAX_LOOP_DEPTH) { perr(P, "loops nested too deep"); return; }
        P->loop_depth++;
        P->exit_stack[P->loop_depth - 1].count = 0;
        while (L->cur.type != T_EOF && L->cur.type != T_WEND && !P->failed)
            parse_stmt(P);
        chunk_emit(P->chunk, OP_JUMP, top, 0);
        int after = P->chunk->ncode;
        P->chunk->code[jf].a = after;
        patch_exits(P, after);
        P->loop_depth--;
        if (L->cur.type != T_WEND) { perr(P, "expected WEND"); return; }
        lex_advance(L);
        return;
    }

    /* FOR ... NEXT */
    if (L->cur.type == T_FOR) {
        lex_advance(L);
        if (L->cur.type != T_IDENT) { perr(P, "FOR: variable expected"); return; }
        char var_name[256];
        strcpy(var_name, L->cur.text);
        int var_slot = chunk_find_global(P->chunk, var_name);
        if (var_slot < 0) { perr(P, "too many globals"); return; }
        lex_advance(L);

        if (L->cur.type != T_EQ) { perr(P, "FOR: expected '='"); return; }
        lex_advance(L);
        parse_expr(P);
        chunk_emit(P->chunk, OP_STORE, var_slot, 0);
        chunk_emit(P->chunk, OP_POP, 0, 0);

        if (L->cur.type != T_TO) { perr(P, "FOR: expected TO"); return; }
        lex_advance(L);

        int for_id = P->loop_depth + 1;
        char end_name[64], step_name[64];
        snprintf(end_name,  sizeof(end_name),  "@for_end_%d",  for_id);
        snprintf(step_name, sizeof(step_name), "@for_step_%d", for_id);
        int end_slot  = chunk_find_global(P->chunk, end_name);
        int step_slot = chunk_find_global(P->chunk, step_name);
        if (end_slot < 0 || step_slot < 0) { perr(P, "too many globals"); return; }

        parse_expr(P);
        chunk_emit(P->chunk, OP_STORE, end_slot, 0);
        chunk_emit(P->chunk, OP_POP, 0, 0);

        if (L->cur.type == T_STEP) {
            lex_advance(L);
            parse_expr(P);
        } else {
            chunk_emit(P->chunk, OP_CONST, chunk_add_const(P->chunk, v_num(1.0)), 0);
        }
        chunk_emit(P->chunk, OP_STORE, step_slot, 0);
        chunk_emit(P->chunk, OP_POP, 0, 0);

        int top = P->chunk->ncode;
        chunk_emit(P->chunk, OP_LOAD, var_slot, 0);
        chunk_emit(P->chunk, OP_LOAD, end_slot, 0);
        chunk_emit(P->chunk, OP_SUB, 0, 0);
        chunk_emit(P->chunk, OP_LOAD, step_slot, 0);
        chunk_emit(P->chunk, OP_MUL, 0, 0);
        chunk_emit(P->chunk, OP_CONST, chunk_add_const(P->chunk, v_num(0.0)), 0);
        chunk_emit(P->chunk, OP_LE, 0, 0);
        int jf = chunk_emit(P->chunk, OP_JUMP_IF_FALSE, 0, 0);

        if (P->loop_depth >= BS_MAX_LOOP_DEPTH) { perr(P, "loops nested too deep"); return; }
        P->loop_depth++;
        P->exit_stack[P->loop_depth - 1].count = 0;

        while (L->cur.type != T_EOF && L->cur.type != T_NEXT && !P->failed)
            parse_stmt(P);

        chunk_emit(P->chunk, OP_LOAD, var_slot, 0);
        chunk_emit(P->chunk, OP_LOAD, step_slot, 0);
        chunk_emit(P->chunk, OP_ADD, 0, 0);
        chunk_emit(P->chunk, OP_STORE, var_slot, 0);
        chunk_emit(P->chunk, OP_POP, 0, 0);
        chunk_emit(P->chunk, OP_JUMP, top, 0);

        int after = P->chunk->ncode;
        P->chunk->code[jf].a = after;
        patch_exits(P, after);
        P->loop_depth--;

        if (L->cur.type != T_NEXT) { perr(P, "expected NEXT"); return; }
        lex_advance(L);
        if (L->cur.type == T_IDENT) {
            if (strcmp(L->cur.text, var_name) != 0) {
                perr(P, "NEXT variable mismatch");
                return;
            }
            lex_advance(L);
        }
        return;
    }

    /* DO ... LOOP */
    if (L->cur.type == T_DO) {
        lex_advance(L);
        int top = P->chunk->ncode;
        if (P->loop_depth >= BS_MAX_LOOP_DEPTH) { perr(P, "loops nested too deep"); return; }
        P->loop_depth++;
        P->exit_stack[P->loop_depth - 1].count = 0;
        while (L->cur.type != T_EOF && L->cur.type != T_LOOP && !P->failed)
            parse_stmt(P);
        chunk_emit(P->chunk, OP_JUMP, top, 0);
        int after = P->chunk->ncode;
        patch_exits(P, after);
        P->loop_depth--;
        if (L->cur.type != T_LOOP) { perr(P, "expected LOOP"); return; }
        lex_advance(L);
        return;
    }

    /* REPEAT ... UNTIL */
    if (L->cur.type == T_REPEAT) {
        lex_advance(L);
        int top = P->chunk->ncode;
        if (P->loop_depth >= BS_MAX_LOOP_DEPTH) { perr(P, "loops nested too deep"); return; }
        P->loop_depth++;
        P->exit_stack[P->loop_depth - 1].count = 0;
        while (L->cur.type != T_EOF && L->cur.type != T_UNTIL && !P->failed)
            parse_stmt(P);
        if (L->cur.type != T_UNTIL) { perr(P, "expected UNTIL"); return; }
        lex_advance(L);
        parse_expr(P);
        chunk_emit(P->chunk, OP_JUMP_IF_FALSE, top, 0);
        int after = P->chunk->ncode;
        patch_exits(P, after);
        P->loop_depth--;
        return;
    }

    /* EXIT | EXIT SUB */
    if (L->cur.type == T_EXIT) {
        lex_advance(L);
        if (L->cur.type == T_SUB) {
            lex_advance(L);
            emit_exit_sub(P);
            return;
        }
        emit_exit(P);
        return;
    }

    /* END */
    if (L->cur.type == T_END) {
        lex_advance(L);
        if (L->cur.type == T_IF) { perr(P, "unexpected END IF"); return; }
        chunk_emit(P->chunk, OP_HALT, 0, 0);
        return;
    }

    perr(P, "unsupported statement");
}

/* ============================================================
 * Компиляция: 3 прохода
 * ============================================================ */

static void resolve_pending(Parser* P) {
    for (int i = 0; i < P->npending; i++) {
        int addr = chunk_find_label(P->chunk, P->pending[i].label);
        if (addr < 0) {
            snprintf(P->S->error, sizeof(P->S->error),
                     "undefined label: %.400s", P->pending[i].label);
            P->S->error_line = P->pending[i].line;
            P->failed = 1;
            return;
        }
        P->chunk->code[P->pending[i].patch_pos].a = addr;
    }
}

/* Pass 1: пройти по исходнику, зарегистрировать заголовки SUB в sub_table */
static int scan_subs(bs_State* S, const char* src) {
    Lexer scan;
    lex_init(&scan, S, src);

    while (scan.cur.type != T_EOF) {
        if (scan.cur.type != T_SUB) { lex_advance(&scan); continue; }

        lex_advance(&scan);
        if (scan.cur.type != T_IDENT) {
            snprintf(S->error, sizeof(S->error), "SUB: name expected");
            S->error_line = scan.cur.line;
            return -1;
        }
        if (S->n_subs_registered >= BS_MAX_SUBS) {
            snprintf(S->error, sizeof(S->error), "too many SUBs");
            return -1;
        }
        for (int i = 0; i < S->n_subs_registered; i++) {
            if (!strcmp(S->sub_table[i].name, scan.cur.text)) {
                snprintf(S->error, sizeof(S->error),
                         "duplicate SUB: %.200s", scan.cur.text);
                S->error_line = scan.cur.line;
                return -1;
            }
        }
        int idx = S->n_subs_registered;
        S->sub_table[idx].name    = strdup(scan.cur.text);
        S->sub_table[idx].idx     = idx;
        S->sub_table[idx].nparams = 0;
        lex_advance(&scan);

        if (scan.cur.type == T_LPAREN) {
            lex_advance(&scan);
            while (scan.cur.type != T_RPAREN && scan.cur.type != T_EOF) {
                if (scan.cur.type != T_IDENT) break;
                lex_advance(&scan);
                int is_arr = 0;
                if (scan.cur.type == T_LPAREN) {
                    lex_advance(&scan);
                    if (scan.cur.type == T_RPAREN) { lex_advance(&scan); is_arr = 1; }
                }
                if (S->sub_table[idx].nparams < BS_MAX_PARAMS)
                    S->sub_table[idx].param_is_array[S->sub_table[idx].nparams] = is_arr;
                S->sub_table[idx].nparams++;
                if (scan.cur.type == T_COMMA) { lex_advance(&scan); continue; }
                break;
            }
            if (scan.cur.type == T_RPAREN) lex_advance(&scan);
        }
        S->n_subs_registered++;

        /* skip body */
        while (scan.cur.type != T_EOF) {
            if (scan.cur.type == T_END && lex_peek(&scan) == T_SUB) {
                lex_advance(&scan);
                lex_advance(&scan);
                break;
            }
            lex_advance(&scan);
        }
    }
    return 0;
}

/* Pass 3: скомпилировать тело SUB с индексом si */
static int compile_one_sub(bs_State* S, const char* src, int si) {
    bs_Chunk* sc = &S->subs[si];
    chunk_init(sc);
    sc->sub_name = strdup(S->sub_table[si].name);

    Parser PS;
    memset(&PS, 0, sizeof(PS));
    PS.S = S;
    PS.chunk = sc;
    PS.in_sub = 1;
    lex_init(&PS.L, S, src);

    /* seek to si-th SUB */
    int counter = 0;
    while (PS.L.cur.type != T_EOF) {
        if (PS.L.cur.type == T_SUB) {
            if (counter == si) {
                lex_advance(&PS.L);
                if (PS.L.cur.type == T_IDENT) lex_advance(&PS.L);
                break;
            }
            counter++;
            skip_sub(&PS);
            if (PS.failed) return -1;
        } else {
            lex_advance(&PS.L);
        }
    }

    /* parse params */
    if (PS.L.cur.type == T_LPAREN) {
        lex_advance(&PS.L);
        while (PS.L.cur.type != T_RPAREN && PS.L.cur.type != T_EOF) {
            if (PS.L.cur.type != T_IDENT) { perr(&PS, "SUB param name"); break; }
            char pname[256];
            strcpy(pname, PS.L.cur.text);
            lex_advance(&PS.L);
            int is_arr = 0;
            if (PS.L.cur.type == T_LPAREN) {
                lex_advance(&PS.L);
                if (PS.L.cur.type == T_RPAREN) { lex_advance(&PS.L); is_arr = 1; }
            }
            int slot = chunk_find_global(sc, pname);
            if (slot < 0) { perr(&PS, "too many locals"); break; }
            sc->params[sc->nparams].name     = strdup(pname);
            sc->params[sc->nparams].is_array = is_arr;
            sc->nparams++;
            if (is_arr) chunk_add_array_decl(sc, pname, slot, 0);
            if (PS.L.cur.type == T_COMMA) { lex_advance(&PS.L); continue; }
            break;
        }
        if (PS.L.cur.type == T_RPAREN) lex_advance(&PS.L);
    }

    /* parse body */
    while (PS.L.cur.type != T_EOF && !PS.failed) {
        if (PS.L.cur.type == T_NEWLINE) { lex_advance(&PS.L); continue; }
        if (PS.L.cur.type == T_END && lex_peek(&PS.L) == T_SUB) {
            lex_advance(&PS.L);
            lex_advance(&PS.L);
            break;
        }
        parse_stmt(&PS);
    }

    if (!PS.failed) resolve_pending(&PS);

    int here = sc->ncode + 1;
    for (int i = 0; i < PS.npending_exits; i++)
        sc->code[PS.pending_exits[i]].a = here;

    if (!PS.failed) chunk_emit(sc, OP_RET_SUB, 0, 0);

    sc->max_locals = sc->nglobals > 0 ? sc->nglobals : 1;

    free(PS.pending);
    free(PS.pending_exits);
    return PS.failed ? -1 : 0;
}

int bs_compile(bs_State* S, const char* source, const char* name) {
    (void)name;
    chunk_free(&S->chunk);
    chunk_init(&S->chunk);
    for (int i = 0; i < S->nsubs; i++) chunk_free(&S->subs[i]);
    for (int i = 0; i < S->n_subs_registered; i++) free(S->sub_table[i].name);
    S->nsubs = 0;
    S->n_subs_registered = 0;
    S->frame_sp = 0;
    S->ret_sp = 0;
    memset(S->is_global, 0, sizeof(S->is_global));

    char* preprocessed = bs_preprocess(S, source, S->include_dir);
    if (!preprocessed) return -1;

    /* Pass 1 */
    if (scan_subs(S, preprocessed) != 0) {
        free(preprocessed);
        return -1;
    }

    /* Pass 2: main */
    Parser P;
    memset(&P, 0, sizeof(P));
    P.S = S;
    P.chunk = &S->chunk;
    lex_init(&P.L, S, preprocessed);

    while (P.L.cur.type != T_EOF && !P.failed) parse_stmt(&P);
    if (!P.failed) resolve_pending(&P);
    if (!P.failed) chunk_emit(&S->chunk, OP_HALT, 0, 0);
    free(P.pending);
    free(P.pending_exits);

    if (P.failed) { free(preprocessed); return -1; }

    /* Pass 3: SUBs */
    for (int si = 0; si < S->n_subs_registered; si++) {
        if (compile_one_sub(S, preprocessed, si) != 0) {
            free(preprocessed);
            return -1;
        }
    }

    S->nsubs = S->n_subs_registered;
    free(preprocessed);
    return 0;
}