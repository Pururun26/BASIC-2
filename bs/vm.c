#include "bs_internal.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void vm_push(bs_State* S, bs_Value v) {
    if (S->sp < BS_STACK_MAX) S->stack[S->sp++] = v;
}
bs_Value vm_pop(bs_State* S)  { return S->sp > 0 ? S->stack[--S->sp] : v_nil(); }
bs_Value vm_peek(bs_State* S) { return S->sp > 0 ? S->stack[S->sp - 1] : v_nil(); }

static int rt_error(bs_State* S, const char* msg) {
    snprintf(S->error, sizeof(S->error), "%s", msg);
    return -1;
}

static bs_Value val_add(bs_State* S, bs_Value a, bs_Value b) {
    if (a.type == BS_STR && b.type == BS_STR)
        return v_str(str_concat(S, a.as.str, b.as.str));
    if (a.type == BS_STR || b.type == BS_STR) {
        char numbuf[64];
        if (a.type == BS_STR) {
            snprintf(numbuf, sizeof(numbuf), "%.14g", v_numof(b));
            bs_String* tmp = str_take(S, numbuf);
            return v_str(str_concat(S, a.as.str, tmp));
        } else {
            snprintf(numbuf, sizeof(numbuf), "%.14g", v_numof(a));
            bs_String* tmp = str_take(S, numbuf);
            return v_str(str_concat(S, tmp, b.as.str));
        }
    }
    return v_num(v_numof(a) + v_numof(b));
}

/* Сравнение значений:
   - строка vs строка   → лексикографически (strcmp)
   - иначе              → численно (v_numof)
   Возврат: -1, 0, +1 */
static int val_cmp(bs_Value a, bs_Value b) {
    if (a.type == BS_STR && b.type == BS_STR)
        return strcmp(a.as.str->chars, b.as.str->chars);
    double da = v_numof(a), db = v_numof(b);
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* Снять все висящие кадры (например, после ошибки предыдущего вызова). */
static void vm_cleanup_frames(bs_State* S) {
    while (S->frame_sp > 0) {
        bs_Frame* f = &S->frames[--S->frame_sp];
        free(f->slots);
        f->slots = NULL;
    }
}

/* ============================================================
 * Основной цикл VM. Используется и bs_execute (для main), и bs_call
 * (для вызова SUB по имени).
 *
 * Особенности:
 *   - frame_sp НЕ сбрасывается — если bs_call уже поставил entry-frame,
 *     цикл его увидит.
 *   - Entry-frame от bs_call имеет chunk == NULL. Когда OP_RET_SUB
 *     (или «падение» за конец чанка) снимает такой кадр — это значит
 *     «вернуться в хост», run_vm возвращает 0.
 * ============================================================ */
static int run_vm(bs_State* S, bs_Chunk* start_chunk) {
    bs_Chunk* cur = start_chunk;
    int pc = 0;

    while (1) {
        if (S->stop_requested) return 0;

        if (pc >= cur->ncode) {
            if (S->frame_sp > 0) {
                bs_Frame* f = &S->frames[--S->frame_sp];
                free(f->slots);
                if (f->chunk == NULL) return 0;    /* entry frame от bs_call */
                cur = f->chunk;
                pc  = f->pc;
                continue;
            }
            return 0;
        }

        if (S->budget > 0 && ++S->used > S->budget)
            return rt_error(S, "instruction budget exceeded");

        bs_Insn in = cur->code[pc++];
        switch (in.op) {
            case OP_NIL:   vm_push(S, v_nil());  break;
            case OP_TRUE:  vm_push(S, v_bool(1)); break;
            case OP_FALSE: vm_push(S, v_bool(0)); break;
            case OP_CONST: vm_push(S, cur->consts[in.a]); break;

            case OP_LOAD: {
                bs_Value* slots = (S->frame_sp > 0)
                    ? S->frames[S->frame_sp - 1].slots
                    : S->globals;
                vm_push(S, slots[in.a]);
            } break;
            case OP_STORE: {
                bs_Value* slots = (S->frame_sp > 0)
                    ? S->frames[S->frame_sp - 1].slots
                    : S->globals;
                slots[in.a] = vm_peek(S);
            } break;
            case OP_LOAD_G:  vm_push(S, S->globals[in.a]); break;
            case OP_STORE_G: S->globals[in.a] = vm_peek(S); break;

            case OP_POP:   (void)vm_pop(S); break;
            case OP_DUP:   vm_push(S, vm_peek(S)); break;

            case OP_ADD: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, val_add(S, a, b)); } break;
            case OP_SUB: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(v_numof(a) - v_numof(b))); } break;
            case OP_MUL: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(v_numof(a) * v_numof(b))); } break;
            case OP_DIV: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(v_numof(a) / v_numof(b))); } break;
            case OP_IDIV:{ bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(floor(v_numof(a) / v_numof(b)))); } break;
            case OP_MOD: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(fmod(v_numof(a), v_numof(b)))); } break;
            case OP_POW: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(pow(v_numof(a), v_numof(b)))); } break;
            case OP_NEG: { bs_Value a = vm_pop(S); vm_push(S, v_num(-v_numof(a))); } break;
            case OP_NOT: { bs_Value a = vm_pop(S); vm_push(S, v_num(v_truthy(a) ? 0 : -1)); } break;

            case OP_EQ: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(val_cmp(a, b) == 0 ? -1 : 0)); } break;
            case OP_NE: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(val_cmp(a, b) != 0 ? -1 : 0)); } break;
            case OP_LT: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(val_cmp(a, b) <  0 ? -1 : 0)); } break;
            case OP_LE: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(val_cmp(a, b) <= 0 ? -1 : 0)); } break;
            case OP_GT: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(val_cmp(a, b) >  0 ? -1 : 0)); } break;
            case OP_GE: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num(val_cmp(a, b) >= 0 ? -1 : 0)); } break;

            case OP_AND: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num((long)v_numof(a) & (long)v_numof(b))); } break;
            case OP_OR:  { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num((long)v_numof(a) | (long)v_numof(b))); } break;
            case OP_XOR: { bs_Value b = vm_pop(S), a = vm_pop(S); vm_push(S, v_num((long)v_numof(a) ^ (long)v_numof(b))); } break;

            case OP_JUMP: pc = in.a; break;
            case OP_JUMP_IF_FALSE: { bs_Value v = vm_pop(S); if (!v_truthy(v)) pc = in.a; } break;

            case OP_GOSUB: {
                if (S->ret_sp >= BS_RET_STACK_MAX) return rt_error(S, "GOSUB stack overflow");
                S->ret_stack[S->ret_sp++] = pc;
                pc = in.a;
            } break;
            case OP_RETURN: {
                if (S->ret_sp <= 0) return rt_error(S, "RETURN without GOSUB");
                pc = S->ret_stack[--S->ret_sp];
            } break;

            case OP_NEW_ARRAY: {
                int ndims = in.b;
                int dims[BS_MAX_ARRAY_DIMS];
                for (int i = ndims - 1; i >= 0; i--) dims[i] = (int)v_numof(vm_pop(S));
                bs_Array* arr = array_new(S, ndims, dims);
                if (!arr) return rt_error(S, "array too large or out of memory");
                bs_Value* slots = (S->frame_sp > 0) ? S->frames[S->frame_sp-1].slots : S->globals;
                slots[in.a] = v_arr(arr);
            } break;

            case OP_INDEX_GET: {
                int ndims = in.a;
                int indices[BS_MAX_ARRAY_DIMS];
                for (int i = ndims - 1; i >= 0; i--) indices[i] = (int)v_numof(vm_pop(S));
                bs_Value av = vm_pop(S);
                if (av.type != BS_ARRAY) return rt_error(S, "not an array");
                bs_Array* arr = av.as.arr;
                if (arr->ndims != ndims) return rt_error(S, "wrong array dimensions");
                int offset = 0, mult = 1;
                for (int i = ndims - 1; i >= 0; i--) {
                    int sz = arr->dims[i] + 1;
                    if (indices[i] < 0 || indices[i] >= sz) return rt_error(S, "array index out of bounds");
                    offset += indices[i] * mult;
                    mult *= sz;
                }
                vm_push(S, arr->data[offset]);
            } break;

            case OP_INDEX_SET: {
                int ndims = in.a;
                bs_Value val = vm_pop(S);
                int indices[BS_MAX_ARRAY_DIMS];
                for (int i = ndims - 1; i >= 0; i--) indices[i] = (int)v_numof(vm_pop(S));
                bs_Value av = vm_pop(S);
                if (av.type != BS_ARRAY) return rt_error(S, "not an array");
                bs_Array* arr = av.as.arr;
                if (arr->ndims != ndims) return rt_error(S, "wrong array dimensions");
                int offset = 0, mult = 1;
                for (int i = ndims - 1; i >= 0; i--) {
                    int sz = arr->dims[i] + 1;
                    if (indices[i] < 0 || indices[i] >= sz) return rt_error(S, "array index out of bounds");
                    offset += indices[i] * mult;
                    mult *= sz;
                }
                arr->data[offset] = val;
            } break;

            case OP_CALL_NATIVE: {
                int idx = in.a, nargs = in.b;
                if (idx < 0 || idx >= S->nnatives) return rt_error(S, "invalid native");
                S->arg_base = S->sp - nargs;
                S->nargs    = nargs;
                int results = S->natives[idx].fn(S);
                int res_at  = S->sp - results;
                memmove(&S->stack[S->arg_base], &S->stack[res_at], results * sizeof(bs_Value));
                S->sp = S->arg_base + results;
            } break;

            case OP_CALL_USER: {
                int sub_idx = in.a, nargs = in.b;
                if (sub_idx < 0 || sub_idx >= S->nsubs) return rt_error(S, "invalid SUB");
                bs_Chunk* subc = &S->subs[sub_idx];
                if (nargs != subc->nparams) return rt_error(S, "arg count mismatch");
                if (S->frame_sp >= BS_CALL_DEPTH) return rt_error(S, "call stack overflow");

                bs_Frame* f = &S->frames[S->frame_sp++];
                f->chunk = cur;
                f->pc    = pc;
                f->slots = calloc(subc->max_locals > 0 ? subc->max_locals : 1, sizeof(bs_Value));
                if (!f->slots) { S->frame_sp--; return rt_error(S, "out of memory"); }
                for (int i = 0; i < nargs; i++) f->slots[i] = S->stack[S->sp - nargs + i];
                S->sp -= nargs;
                for (int i = nargs; i < subc->max_locals; i++) f->slots[i] = v_nil();

                cur = subc;
                pc  = 0;
            } break;

            case OP_RET_SUB: {
                if (S->frame_sp <= 0) return rt_error(S, "RET_SUB without CALL");
                bs_Frame* f = &S->frames[--S->frame_sp];
                free(f->slots);
                if (f->chunk == NULL) return 0;    /* entry frame от bs_call */
                cur = f->chunk;
                pc  = f->pc;
            } break;

            case OP_HALT:
                /* Если каким-то образом HALT попал в SUB (END внутри SUB),
                   снимаем оставшиеся кадры, чтобы не текло. */
                vm_cleanup_frames(S);
                return 0;

            default: return rt_error(S, "bad opcode");
        }
    }
}

/* ============================================================
 * Публичный API
 * ============================================================ */

/* Выполнить main-чанк с чистого листа. */
int bs_execute(bs_State* S) {
    S->error[0] = 0;
    S->error_line = 0;
    S->sp = 0;
    S->used = 0;
    S->ret_sp = 0;
    S->stop_requested = 0;
    vm_cleanup_frames(S);

    return run_vm(S, &S->chunk);
}

/* Вызвать SUB по имени. Аргументы уже на стеке (через bs_push_*);
 * nargs — сколько снять со стека. Возврат: 0 = успех, -1 = ошибка. */
int bs_call(bs_State* S, const char* name, int nargs) {
    /* Найти SUB */
    int idx = -1;
    for (int i = 0; i < S->n_subs_registered; i++)
        if (!strcmp(S->sub_table[i].name, name)) { idx = i; break; }
    if (idx < 0) {
        snprintf(S->error, sizeof(S->error), "SUB not found: %s", name);
        S->error_line = 0;
        return -1;
    }

    bs_Chunk* sc = &S->subs[idx];
    if (nargs != sc->nparams) {
        snprintf(S->error, sizeof(S->error),
                 "SUB %s: expected %d args, got %d",
                 name, sc->nparams, nargs);
        S->error_line = 0;
        return -1;
    }
    if (S->frame_sp >= BS_CALL_DEPTH) {
        snprintf(S->error, sizeof(S->error), "call stack overflow");
        S->error_line = 0;
        return -1;
    }

    /* Defensive: снять висящие кадры от предыдущей ошибки */
    vm_cleanup_frames(S);

    /* Entry-frame: chunk = NULL — маркер «вернуться в хост». */
    bs_Frame* f = &S->frames[S->frame_sp++];
    f->chunk = NULL;
    f->pc    = 0;
    int nloc = sc->max_locals > 0 ? sc->max_locals : 1;
    f->slots = calloc(nloc, sizeof(bs_Value));
    if (!f->slots) {
        S->frame_sp--;
        snprintf(S->error, sizeof(S->error), "out of memory");
        S->error_line = 0;
        return -1;
    }
    for (int i = 0; i < sc->max_locals; i++)
        f->slots[i] = (i < nargs) ? S->stack[S->sp - nargs + i] : v_nil();
    S->sp -= nargs;

    S->error[0] = 0;
    S->error_line = 0;
    S->used = 0;
    S->stop_requested = 0;

    return run_vm(S, sc);
}