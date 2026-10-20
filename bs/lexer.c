#include "lexer.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void skip_ws(Lexer* L) {
    while (L->src[L->pos] == ' ' || L->src[L->pos] == '\t' || L->src[L->pos] == '\r')
        L->pos++;
}

static void skip_comment(Lexer* L) {
    while (L->src[L->pos] && L->src[L->pos] != '\n') L->pos++;
}

static void lex_next(Lexer* L, Token* T) {
    memset(T, 0, sizeof(*T));
    skip_ws(L);
    char c = L->src[L->pos];

    if (c == 0)    { T->type = T_EOF;     T->line = L->line; return; }
    if (c == '\n') { L->pos++; T->type = T_NEWLINE; T->line = L->line++; return; }
    if (c == '\'') { skip_comment(L); lex_next(L, T); return; }

    T->line = L->line;

    if (isdigit(c) || (c == '.' && isdigit(L->src[L->pos+1]))) {
        char* end; T->num = strtod(L->src + L->pos, &end); L->pos = end - L->src;
        T->type = T_NUMBER; return;
    }
    if (c == '$' && isxdigit(L->src[L->pos+1])) {
        L->pos++; char* end;
        T->num = (double)strtol(L->src + L->pos, &end, 16); L->pos = end - L->src;
        T->type = T_NUMBER; return;
    }
    if (c == '%' && (L->src[L->pos+1] == '0' || L->src[L->pos+1] == '1')) {
        L->pos++; char* end;
        T->num = (double)strtol(L->src + L->pos, &end, 2); L->pos = end - L->src;
        T->type = T_NUMBER; return;
    }

    /* строки: """...""" или "..." */
    if (c == '"') {
        /* многострочный литерал """...""" */
        if (L->src[L->pos+1] == '"' && L->src[L->pos+2] == '"') {
            L->pos += 3;
            int start = L->pos;
            while (L->src[L->pos]) {
                if (L->src[L->pos]   == '"' &&
                    L->src[L->pos+1] == '"' &&
                    L->src[L->pos+2] == '"') break;
                if (L->src[L->pos] == '\n') L->line++;
                L->pos++;
            }
            int len = L->pos - start;
            if (len > 1023) len = 1023;
            memcpy(T->text, L->src + start, len);
            T->text[len] = '\0';
            if (L->src[L->pos] == '"') L->pos += 3;
            T->type = T_LONGSTRING;
            return;
        }
        /* обычная строка "..." */
        L->pos++;
        int start = L->pos;
        while (L->src[L->pos] && L->src[L->pos] != '"') {
            if (L->src[L->pos] == '\n') L->line++;
            L->pos++;
        }
        int len = L->pos - start;
        if (len > 1023) len = 1023;
        memcpy(T->text, L->src + start, len);
        T->text[len] = 0;
        if (L->src[L->pos] == '"') L->pos++;
        T->type = T_STRING;
        return;
    }

    if (isalpha(c) || c == '_') {
        int start = L->pos;
        while (isalnum(L->src[L->pos]) || L->src[L->pos] == '_') L->pos++;
        if (L->src[L->pos] == '$') L->pos++;
        int len = L->pos - start;
        if (len > 255) len = 255;
        memcpy(T->text, L->src + start, len);
        T->text[len] = 0;

        char up[256];
        for (int i = 0; i <= len; i++) up[i] = toupper((unsigned char)T->text[i]);

        if      (!strcmp(up, "REM"))    { skip_comment(L); lex_next(L, T); return; }
        else if (!strcmp(up, "IF"))     T->type = T_IF;
        else if (!strcmp(up, "THEN"))   T->type = T_THEN;
        else if (!strcmp(up, "ELSE"))   T->type = T_ELSE;
        else if (!strcmp(up, "END"))    T->type = T_END;
        else if (!strcmp(up, "WHILE"))  T->type = T_WHILE;
        else if (!strcmp(up, "WEND"))   T->type = T_WEND;
        else if (!strcmp(up, "FOR"))    T->type = T_FOR;
        else if (!strcmp(up, "TO"))     T->type = T_TO;
        else if (!strcmp(up, "STEP"))   T->type = T_STEP;
        else if (!strcmp(up, "NEXT"))   T->type = T_NEXT;
        else if (!strcmp(up, "DO"))     T->type = T_DO;
        else if (!strcmp(up, "LOOP"))   T->type = T_LOOP;
        else if (!strcmp(up, "REPEAT")) T->type = T_REPEAT;
        else if (!strcmp(up, "UNTIL"))  T->type = T_UNTIL;
        else if (!strcmp(up, "SUB"))    T->type = T_SUB;
        else if (!strcmp(up, "CALL"))   T->type = T_CALL;
        else if (!strcmp(up, "EXIT"))   T->type = T_EXIT;
        else if (!strcmp(up, "GOTO"))   T->type = T_GOTO;
        else if (!strcmp(up, "GOSUB"))  T->type = T_GOSUB;
        else if (!strcmp(up, "RETURN")) T->type = T_RETURN;
        else if (!strcmp(up, "GLOBAL")) T->type = T_GLOBAL;
        else if (!strcmp(up, "DIM"))    T->type = T_DIM;
        else if (!strcmp(up, "LET"))    T->type = T_LET;
        else if (!strcmp(up, "INPUT"))  T->type = T_INPUT;
        else if (!strcmp(up, "MOD"))    T->type = T_MOD;
        else if (!strcmp(up, "AND"))    T->type = T_AND;
        else if (!strcmp(up, "OR"))     T->type = T_OR;
        else if (!strcmp(up, "XOR"))    T->type = T_XOR;
        else if (!strcmp(up, "NOT"))    T->type = T_NOT;
        else if (!strcmp(up, "PI"))     T->type = T_PI;
        else if (!strcmp(up, "SWAP"))   T->type = T_SWAP;
        else {
            T->type = T_IDENT;
            int save = L->pos;
            skip_ws(L);
            if (L->src[L->pos] == ':') { L->pos++; T->type = T_LABEL; }
            else L->pos = save;
        }
        return;
    }

    L->pos++;
    switch (c) {
        case '+': T->type = T_PLUS;   break;
        case '-': T->type = T_MINUS;  break;
        case '*': T->type = T_STAR;   break;
        case '/': T->type = T_SLASH;  break;
        case '\\':T->type = T_BSLASH; break;
        case '^': T->type = T_CARET;  break;
        case '(': T->type = T_LPAREN; break;
        case ')': T->type = T_RPAREN; break;
        case ',': T->type = T_COMMA;  break;
        case ';': T->type = T_SEMI;   break;
        case ':': T->type = T_COLON;  break;
        case '=': T->type = T_EQ;     break;
        case '<':
            if (L->src[L->pos] == '=') { L->pos++; T->type = T_LE; }
            else if (L->src[L->pos] == '>') { L->pos++; T->type = T_NE; }
            else T->type = T_LT;
            break;
        case '>':
            if (L->src[L->pos] == '=') { L->pos++; T->type = T_GE; }
            else T->type = T_GT;
            break;
        default:
            T->type = T_ERROR;
            snprintf(T->text, sizeof(T->text), "unexpected char '%c'", c);
    }
}

void lex_init(Lexer* L, bs_State* S, const char* src) {
    memset(L, 0, sizeof(*L));
    L->S = S;
    L->src = src;
    L->pos = 0;
    L->line = 1;
    lex_next(L, &L->cur);
}

void lex_advance(Lexer* L) {
    if (L->has_next) { L->cur = L->next; L->has_next = 0; }
    else             { lex_next(L, &L->cur); }
}

TokType lex_peek(Lexer* L) {
    if (!L->has_next) { lex_next(L, &L->next); L->has_next = 1; }
    return L->next.type;
}