#include "bs_internal.h"
#include <stdlib.h>
#include <string.h>

bs_State* bs_new(void) {
    bs_State* S = calloc(1, sizeof(bs_State));
    S->gc_threshold = 256 * 1024;
    for (int i = 0; i < BS_GLOBALS_MAX; i++) S->globals[i] = v_nil();
    bs_register_builtins_math(S);
    bs_register_builtins_string(S);
    return S;
}

void bs_stop(bs_State* S) {
    S->stop_requested = 1;
}

void bs_free(bs_State* S) {
    if (!S) return;
    for (int i = 0; i < S->nnatives; i++) free(S->natives[i].name);
    chunk_free(&S->chunk);
    for (int i = 0; i < S->nsubs; i++) chunk_free(&S->subs[i]);
    for (int i = 0; i < S->n_subs_registered; i++) free(S->sub_table[i].name);
    for (int i = 0; i < S->frame_sp; i++) free(S->frames[i].slots);
    bs_GCObj* o = S->gc_head;
    while (o) { bs_GCObj* n = o->next; free(o); o = n; }
    free(S->include_dir);
    free(S);
}

int bs_run(bs_State* S, const char* source) {
    if (bs_compile(S, source, "main") < 0) return -1;
    return bs_execute(S);
}

void bs_register_native(bs_State* S, const char* name, bs_NativeFn fn) {
    if (S->nnatives >= BS_NATIVES_MAX) return;
    S->natives[S->nnatives].name = strdup(name);
    S->natives[S->nnatives].fn   = fn;
    S->nnatives++;
}

int bs_argc(bs_State* S) { return S->nargs; }
bs_Value bs_arg(bs_State* S, int i) {
    if (i < 0 || i >= S->nargs) return v_nil();
    return S->stack[S->arg_base + i];
}
double bs_arg_number(bs_State* S, int i) { return v_numof(bs_arg(S, i)); }
const char* bs_arg_string(bs_State* S, int i) {
    bs_Value v = bs_arg(S, i);
    return (v.type == BS_STR && v.as.str) ? v.as.str->chars : "";
}

void bs_push_number(bs_State* S, double v) { vm_push(S, v_num(v)); }
void bs_push_string(bs_State* S, const char* s) { vm_push(S, v_str(str_take(S, s))); }
void bs_push_nil(bs_State* S)               { vm_push(S, v_nil()); }
void bs_push_bool(bs_State* S, int b)       { vm_push(S, v_bool(b)); }

int bs_value_is_string(bs_Value v) { return v.type == BS_STR; }
const char* bs_string_chars(bs_String* s) { return s ? s->chars : ""; }

const char* bs_error(bs_State* S)      { return S->error; }
int         bs_error_line(bs_State* S) { return S->error_line; }
void        bs_set_budget(bs_State* S, int64_t b) { S->budget = b; }

void bs_set_include_dir(bs_State* S, const char* dir) {
    free(S->include_dir);
    S->include_dir = dir ? strdup(dir) : NULL;
}