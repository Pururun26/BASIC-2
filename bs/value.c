#include "bs_internal.h"
#include <stdlib.h>

bs_Value v_nil(void)        { bs_Value v; v.type = BS_NIL;   v.as.num = 0; return v; }
bs_Value v_bool(int b)      { bs_Value v; v.type = BS_BOOL;  v.as.boolean = b ? 1 : 0; return v; }
bs_Value v_num(double n)    { bs_Value v; v.type = BS_NUM;   v.as.num = n; return v; }
bs_Value v_str(bs_String* s){ bs_Value v; v.type = BS_STR;   v.as.str = s; return v; }
bs_Value v_arr(bs_Array* a) { bs_Value v; v.type = BS_ARRAY; v.as.arr = a; return v; }

int v_truthy(bs_Value v) {
    switch (v.type) {
        case BS_NIL:    return 0;
        case BS_BOOL:   return v.as.boolean != 0;
        case BS_NUM:    return v.as.num != 0.0;
        case BS_STR:    return v.as.str && v.as.str->len > 0;
        case BS_ARRAY:  return v.as.arr != NULL;
        default:        return 1;
    }
}

double v_numof(bs_Value v) {
    if (v.type == BS_NUM)   return v.as.num;
    if (v.type == BS_BOOL)  return v.as.boolean ? -1.0 : 0.0;
    if (v.type == BS_STR)   return atof(v.as.str->chars);
    return 0.0;
}