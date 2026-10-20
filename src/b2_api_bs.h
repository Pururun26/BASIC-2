#pragma once

#include "bs.h"

/* Регистрирует все нативы BASIC-2 в bs_State.
   Вызывать ПОСЛЕ bs_new() и ДО bs_compile_file / bs_run. */
void b2_register_bs_api(bs_State* S);