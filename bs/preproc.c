#include "preproc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BS_INCLUDE_MAX_DEPTH 16

/* ============================================================
 * Буфер
 * ============================================================ */
typedef struct { char* data; size_t len; size_t cap; } Buf;

static void buf_init(Buf* b) { b->data = NULL; b->len = 0; b->cap = 0; }

static void buf_append(Buf* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap : 256;
        while (b->cap < b->len + n + 1) b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

/* ============================================================
 * Файлы и пути
 * ============================================================ */
static char* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char* data = malloc((size_t)sz + 1);
    size_t got = fread(data, 1, (size_t)sz, f);
    fclose(f);
    data[got] = 0;
    if (out_size) *out_size = got;
    return data;
}

static char* dir_of(const char* path) {
    const char* last = NULL;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') last = p;
    if (!last) return NULL;
    size_t n = (size_t)(last - path);
    char* d = malloc(n + 1);
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

static int is_absolute(const char* path) {
    if (path[0] == '/' || path[0] == '\\') return 1;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return 1;
    return 0;
}

static char* join_path(const char* base, const char* rel) {
    if (!base || is_absolute(rel)) return strdup(rel);
    size_t bl = strlen(base), rl = strlen(rel);
    int sep = (bl > 0 && base[bl-1] != '/' && base[bl-1] != '\\');
    char* out = malloc(bl + (sep ? 1 : 0) + rl + 1);
    memcpy(out, base, bl);
    size_t pos = bl;
    if (sep) out[pos++] = '/';
    memcpy(out + pos, rel, rl + 1);
    return out;
}

/* ============================================================
 * Распознавание $INCLUDE
 * ============================================================ */
static int match_include(const char* line, size_t linelen,
                         char* out_path, size_t out_max) {
    size_t i = 0;
    while (i < linelen && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= linelen || line[i] != '$') return 0;
    i++;
    static const char kw[] = "INCLUDE";
    for (size_t k = 0; kw[k]; k++, i++) {
        if (i >= linelen) return 0;
        if (toupper((unsigned char)line[i]) != (unsigned char)kw[k]) return 0;
    }
    while (i < linelen && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= linelen || line[i] != '"') return 0;
    i++;
    size_t start = i;
    while (i < linelen && line[i] != '"') i++;
    if (i >= linelen) return 0;
    size_t plen = i - start;
    if (plen == 0 || plen >= out_max) return 0;
    memcpy(out_path, line + start, plen);
    out_path[plen] = 0;
    i++;
    while (i < linelen && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i < linelen && line[i] != '\r') return 0;
    return 1;
}

/* ============================================================
 * Рекурсивный обход
 * ============================================================ */
static int preprocess_inner(bs_State* S, const char* source,
                            const char* base_dir, int depth, Buf* out) {
    if (depth > BS_INCLUDE_MAX_DEPTH) {
        snprintf(S->error, sizeof(S->error),
                 "$INCLUDE nesting too deep (>%d)", BS_INCLUDE_MAX_DEPTH);
        return -1;
    }

    size_t n = strlen(source);
    size_t pos = 0;
    while (pos < n) {
        size_t eol = pos;
        while (eol < n && source[eol] != '\n') eol++;
        size_t linelen = eol - pos;

        char inc[512];
        if (match_include(source + pos, linelen, inc, sizeof(inc))) {
            char* full = join_path(base_dir, inc);
            size_t sz = 0;
            char* content = read_file(full, &sz);
            if (!content) {
                snprintf(S->error, sizeof(S->error),
                         "cannot open include file: %s", full);
                free(full);
                return -1;
            }
            char* sub = dir_of(full);
            int rc = preprocess_inner(S, content, sub ? sub : base_dir, depth + 1, out);
            free(sub);
            free(content);
            free(full);
            if (rc != 0) return -1;
        } else {
            buf_append(out, source + pos, linelen);
            if (eol < n) buf_append(out, "\n", 1);
        }
        pos = (eol < n) ? eol + 1 : n;
    }
    return 0;
}

char* bs_preprocess(bs_State* S, const char* source, const char* base_dir) {
    Buf out;
    buf_init(&out);
    if (preprocess_inner(S, source, base_dir, 0, &out) != 0) {
        free(out.data);
        return NULL;
    }
    if (!out.data) buf_append(&out, "", 0);
    return out.data;
}

/* ============================================================
 * Файловые точки входа (публичные API — см. bs.h)
 * ============================================================ */
int bs_compile_file(bs_State* S, const char* path) {
    size_t sz = 0;
    char* src = read_file(path, &sz);
    if (!src) {
        snprintf(S->error, sizeof(S->error), "cannot open file: %s", path);
        return -1;
    }
    char* dir = dir_of(path);
    char* saved = S->include_dir;
    S->include_dir = dir;
    int rc = bs_compile(S, src, path);
    S->include_dir = saved;
    free(dir);
    free(src);
    return rc;
}

int bs_run_file(bs_State* S, const char* path) {
    if (bs_compile_file(S, path) != 0) return -1;
    return bs_execute(S);
}