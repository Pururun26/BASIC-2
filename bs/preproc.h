#ifndef BS_PREPROC_H
#define BS_PREPROC_H

#include "bs_internal.h"

/* Разворачивает $INCLUDE рекурсивно.
   Возвращает malloc'd строку (free() обязателен), либо NULL при ошибке
   (сообщение в S->error). base_dir == NULL — относительные пути от CWD. */
char* bs_preprocess(bs_State* S, const char* source, const char* base_dir);

#endif