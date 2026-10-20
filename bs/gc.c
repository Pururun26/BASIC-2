#include "bs_internal.h"
#include <stdlib.h>

void gc_mark_array(bs_Array* a) {
    if (a->gc.marked) return;
    a->gc.marked = 1;
    for (int i = 0; i < a->total; i++) gc_mark_value(a->data[i]);
}

void gc_mark_value(bs_Value v) {
    if (v.type == BS_STR   && v.as.str) v.as.str->gc.marked = 1;
    else if (v.type == BS_ARRAY && v.as.arr) gc_mark_array(v.as.arr);
}

void gc_add(bs_State* S, void* obj, int type, size_t size) {
    bs_GCObj* o = (bs_GCObj*)obj;   /* gc is first field */
    o->type   = type;
    o->marked = 0;
    o->size   = size;
    o->next   = S->gc_head;
    S->gc_head = o;
    S->gc_bytes += size;
}

void gc_collect(bs_State* S) {
    /* mark roots */
    for (int i = 0; i < S->chunk.nconsts; i++)  gc_mark_value(S->chunk.consts[i]);
    for (int i = 0; i < BS_GLOBALS_MAX; i++)    gc_mark_value(S->globals[i]);
    for (int i = 0; i < S->sp; i++)             gc_mark_value(S->stack[i]);

    /* sweep */
    size_t kept = 0;
    bs_GCObj** p = &S->gc_head;
    while (*p) {
        bs_GCObj* o = *p;
        if (!o->marked) {
            *p = o->next;
            free(o);   /* gc is first field → frees whole allocation */
        } else {
            o->marked = 0;
            kept += o->size;
            p = &o->next;
        }
    }
    S->gc_bytes = kept;
}