#ifndef BS_INTERNAL_H
#define BS_INTERNAL_H

#include "bs.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BS_MAX_ARRAY_DIMS 3
#define BS_MAX_TOTAL     1000000
#define BS_MAX_SUBS      64
#define BS_CALL_DEPTH    64
#define BS_MAX_PARAMS    16
#define BS_MAX_LOCALS    256

enum { BS_GC_STRING = 0, BS_GC_ARRAY = 1 };

typedef struct bs_GCObj {
    struct bs_GCObj* next;
    int     type;
    int     marked;
    size_t  size;
} bs_GCObj;

struct bs_String {
    bs_GCObj gc;
    int      len;
    char     chars[];
};

struct bs_Array {
    bs_GCObj gc;
    int      ndims;
    int      dims[BS_MAX_ARRAY_DIMS];
    int      total;
    bs_Value data[];
};

typedef struct {
    char* name;
    int   slot;
    int   ndims;
} bs_ArrayDecl;

typedef struct { char* name; int addr; } bs_Label;

typedef struct { char* name; int is_array; } bs_Param;

enum {
    OP_NIL, OP_TRUE, OP_FALSE, OP_CONST,
    OP_LOAD, OP_STORE, OP_POP, OP_DUP,
    OP_LOAD_G, OP_STORE_G,              /* GLOBAL access from SUB */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_IDIV, OP_MOD, OP_POW,
    OP_NEG, OP_NOT,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR, OP_XOR,
    OP_JUMP, OP_JUMP_IF_FALSE,
    OP_GOSUB, OP_RETURN,
    OP_NEW_ARRAY, OP_INDEX_GET, OP_INDEX_SET,
    OP_CALL_NATIVE,
    OP_CALL_USER, OP_RET_SUB,           /* SUB call/return */
    OP_HALT
};

typedef struct { uint8_t op; int32_t a; int32_t b; } bs_Insn;

typedef struct {
    char* sub_name;   /* NULL для main */

    bs_Param params[BS_MAX_PARAMS];
    int      nparams;

    bs_Insn*  code;
    int       ncode, cap;
    bs_Value* consts;
    int       nconsts, ccap;

    char**    globals;      /* slot → имя */
    int       nglobals, gcap;
    int       max_locals;   /* = nglobals, размер frame.slots */

    bs_Label* labels;
    int       nlabels, lcap;

    bs_ArrayDecl* array_decls;
    int           n_array_decls, cap_array_decls;
} bs_Chunk;

typedef struct { char* name; bs_NativeFn fn; } bs_Native;

typedef struct {
    char* name;
    int   idx;
    int   nparams;
    int   param_is_array[BS_MAX_PARAMS];
} bs_SubEntry;

typedef struct {
    bs_Chunk* chunk;    /* caller */
    int       pc;
    bs_Value* slots;    /* malloc, размер chunk->max_locals */
} bs_Frame;

#define BS_STACK_MAX    4096
#define BS_GLOBALS_MAX  256
#define BS_NATIVES_MAX  128
#define BS_RET_STACK_MAX 256

struct bs_State {
    bs_Value  stack[BS_STACK_MAX];
    int       sp;
    int       arg_base, nargs;

    int       stop_requested;

    bs_Value  globals[BS_GLOBALS_MAX];
    int       is_global[BS_GLOBALS_MAX];   /* 1 = GLOBAL-директива */

    bs_Native natives[BS_NATIVES_MAX];
    int       nnatives;

    bs_Chunk  chunk;                     /* main */
    bs_Chunk  subs[BS_MAX_SUBS];         /* SUB chunks */
    int       nsubs;

    bs_SubEntry sub_table[BS_MAX_SUBS];
    int         n_subs_registered;

    bs_Frame  frames[BS_CALL_DEPTH];
    int       frame_sp;

    int       ret_stack[BS_RET_STACK_MAX];
    int       ret_sp;

    bs_GCObj* gc_head;
    size_t    gc_bytes;
    size_t    gc_threshold;

    char      error[512];
    int       error_line;

    int64_t   budget, used;
    char*     include_dir;
};

/* value.c */
bs_Value v_nil(void);
bs_Value v_bool(int b);
bs_Value v_num(double n);
bs_Value v_str(bs_String* s);
bs_Value v_arr(bs_Array* a);
int      v_truthy(bs_Value v);
double   v_numof(bs_Value v);

/* string.c */
bs_String* str_new(bs_State* S, const char* src, int len);
bs_String* str_take(bs_State* S, const char* cstr);
bs_String* str_concat(bs_State* S, bs_String* a, bs_String* b);
bs_Array*  array_new(bs_State* S, int ndims, const int* dims);

/* gc.c */
void gc_mark_value(bs_Value v);
void gc_mark_array(bs_Array* a);
void gc_add(bs_State* S, void* obj_with_gc_header, int type, size_t size);
void gc_collect(bs_State* S);

/* chunk.c */
void chunk_init(bs_Chunk* c);
void chunk_free(bs_Chunk* c);
int  chunk_emit(bs_Chunk* c, uint8_t op, int32_t a, int32_t b);
int  chunk_add_const(bs_Chunk* c, bs_Value v);
int  chunk_find_global(bs_Chunk* c, const char* name);
int  chunk_lookup_global(bs_Chunk* c, const char* name);
int  chunk_find_native(bs_State* S, const char* name);
int  chunk_find_global_in_main(bs_State* S, const char* name);   /* -1 если нет или не GLOBAL */
int  chunk_add_label(bs_Chunk* c, const char* name, int addr);
int  chunk_find_label(bs_Chunk* c, const char* name);
int  chunk_add_array_decl(bs_Chunk* c, const char* name, int slot, int ndims);
bs_ArrayDecl* chunk_find_array_decl(bs_Chunk* c, const char* name);

/* vm.c */
void     vm_push(bs_State* S, bs_Value v);
bs_Value vm_pop(bs_State* S);
bs_Value vm_peek(bs_State* S);

/* state.c */
void bs_register_builtins_math(bs_State* S);
void bs_register_builtins_string(bs_State* S);

#endif