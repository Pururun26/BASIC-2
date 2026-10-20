#include "bs_internal.h"
#include <string.h>
#include <stdlib.h>

void chunk_init(bs_Chunk* c) { memset(c, 0, sizeof(*c)); }

void chunk_free(bs_Chunk* c) {
    for (int i = 0; i < c->nglobals; i++) free(c->globals[i]);
    for (int i = 0; i < c->nlabels; i++) free(c->labels[i].name);
    for (int i = 0; i < c->n_array_decls; i++) free(c->array_decls[i].name);
    free(c->globals);
    free(c->labels);
    free(c->array_decls);
    free(c->code);
    free(c->consts);
    if (c->sub_name) free(c->sub_name);
    for (int i = 0; i < c->nparams; i++) free(c->params[i].name);
    memset(c, 0, sizeof(*c));
}

int chunk_emit(bs_Chunk* c, uint8_t op, int32_t a, int32_t b) {
    if (c->ncode >= c->cap) {
        c->cap = c->cap ? c->cap * 2 : 64;
        c->code = realloc(c->code, c->cap * sizeof(bs_Insn));
    }
    c->code[c->ncode].op = op;
    c->code[c->ncode].a  = a;
    c->code[c->ncode].b  = b;
    return c->ncode++;
}

int chunk_add_const(bs_Chunk* c, bs_Value v) {
    if (c->nconsts >= c->ccap) {
        c->ccap = c->ccap ? c->ccap * 2 : 16;
        c->consts = realloc(c->consts, c->ccap * sizeof(bs_Value));
    }
    c->consts[c->nconsts] = v;
    return c->nconsts++;
}

/* ============================================================
 * Глобалы (chunk-local)
 * ============================================================ */

int chunk_lookup_global(bs_Chunk* c, const char* name) {
    for (int i = 0; i < c->nglobals; i++)
        if (!strcmp(c->globals[i], name)) return i;
    return -1;
}

int chunk_find_global(bs_Chunk* c, const char* name) {
    int slot = chunk_lookup_global(c, name);
    if (slot >= 0) return slot;
    if (c->nglobals >= BS_GLOBALS_MAX) return -1;
    if (c->nglobals >= c->gcap) {
        c->gcap = c->gcap ? c->gcap * 2 : 16;
        c->globals = realloc(c->globals, c->gcap * sizeof(char*));
    }
    c->globals[c->nglobals] = strdup(name);
    return c->nglobals++;
}

/* ============================================================
 * Нативы
 * ============================================================ */

int chunk_find_native(bs_State* S, const char* name) {
    for (int i = 0; i < S->nnatives; i++)
        if (!strcmp(S->natives[i].name, name)) return i;
    return -1;
}

/* ============================================================
 * Метки
 * ============================================================ */

int chunk_add_label(bs_Chunk* c, const char* name, int addr) {
    for (int i = 0; i < c->nlabels; i++)
        if (!strcmp(c->labels[i].name, name)) return -2;    /* duplicate */

    if (c->nlabels >= c->lcap) {
        c->lcap = c->lcap ? c->lcap * 2 : 8;
        c->labels = realloc(c->labels, c->lcap * sizeof(bs_Label));
    }
    c->labels[c->nlabels].name = strdup(name);
    c->labels[c->nlabels].addr = addr;
    c->nlabels++;
    return 0;
}

int chunk_find_label(bs_Chunk* c, const char* name) {
    for (int i = 0; i < c->nlabels; i++)
        if (!strcmp(c->labels[i].name, name)) return c->labels[i].addr;
    return -1;
}

/* ============================================================
 * Массивы
 * ============================================================ */

int chunk_add_array_decl(bs_Chunk* c, const char* name, int slot, int ndims) {
    for (int i = 0; i < c->n_array_decls; i++)
        if (!strcmp(c->array_decls[i].name, name)) return -2;   /* duplicate */

    if (c->n_array_decls >= c->cap_array_decls) {
        c->cap_array_decls = c->cap_array_decls ? c->cap_array_decls * 2 : 8;
        c->array_decls = realloc(c->array_decls,
                                 c->cap_array_decls * sizeof(bs_ArrayDecl));
    }
    c->array_decls[c->n_array_decls].name  = strdup(name);
    c->array_decls[c->n_array_decls].slot  = slot;
    c->array_decls[c->n_array_decls].ndims = ndims;
    c->n_array_decls++;
    return 0;
}

bs_ArrayDecl* chunk_find_array_decl(bs_Chunk* c, const char* name) {
    for (int i = 0; i < c->n_array_decls; i++)
        if (!strcmp(c->array_decls[i].name, name)) return &c->array_decls[i];
    return NULL;
}

/* ============================================================
 * GLOBAL-переменные main (видимы из SUB)
 * ============================================================ */

int chunk_find_global_in_main(bs_State* S, const char* name) {
    for (int i = 0; i < S->chunk.nglobals; i++)
        if (S->is_global[i] && !strcmp(S->chunk.globals[i], name))
            return i;
    return -1;
}