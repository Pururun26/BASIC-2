#include "bs_internal.h"
#include <string.h>
#include <stdlib.h>

bs_String* str_new(bs_State* S, const char* src, int len) {
    if (S->gc_bytes > S->gc_threshold) gc_collect(S);
    bs_String* s = malloc(sizeof(bs_String) + len + 1);
    s->len = len;
    memcpy(s->chars, src, len);
    s->chars[len] = '\0';
    gc_add(S, s, BS_GC_STRING, sizeof(bs_String) + len + 1);
    return s;
}

bs_String* str_take(bs_State* S, const char* cstr) {
    return str_new(S, cstr, (int)strlen(cstr));
}

bs_String* str_concat(bs_State* S, bs_String* a, bs_String* b) {
    int len = a->len + b->len;
    if (S->gc_bytes > S->gc_threshold) gc_collect(S);
    bs_String* s = malloc(sizeof(bs_String) + len + 1);
    s->len = len;
    memcpy(s->chars, a->chars, a->len);
    memcpy(s->chars + a->len, b->chars, b->len);
    s->chars[len] = '\0';
    gc_add(S, s, BS_GC_STRING, sizeof(bs_String) + len + 1);
    return s;
}

bs_Array* array_new(bs_State* S, int ndims, const int* dims) {
    int total = 1;
    for (int i = 0; i < ndims; i++) {
        if (dims[i] < 0) return NULL;
        total *= (dims[i] + 1);
        if (total > BS_MAX_TOTAL) return NULL;
    }
    if (S->gc_bytes > S->gc_threshold) gc_collect(S);
    size_t bytes = sizeof(bs_Array) + (size_t)total * sizeof(bs_Value);
    bs_Array* a = malloc(bytes);
    a->ndims = ndims;
    for (int i = 0; i < BS_MAX_ARRAY_DIMS; i++) a->dims[i] = (i < ndims) ? dims[i] : 0;
    a->total = total;
    for (int i = 0; i < total; i++) a->data[i] = v_nil();
    gc_add(S, a, BS_GC_ARRAY, bytes);
    return a;
}