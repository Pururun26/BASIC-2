#ifndef BS_LEXER_H
#define BS_LEXER_H

#include "bs_internal.h"

typedef enum {
    T_EOF, T_NEWLINE, T_NUMBER, T_STRING, T_IDENT, T_LABEL,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_BSLASH, T_CARET,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_LPAREN, T_RPAREN, T_COMMA, T_SEMI, T_COLON,
    T_IF, T_THEN, T_ELSE, T_END, T_WHILE, T_WEND,
    T_FOR, T_TO, T_STEP, T_NEXT, T_DO, T_LOOP, T_REPEAT, T_UNTIL,
    T_SUB, T_CALL, T_EXIT, T_GOTO, T_GOSUB, T_RETURN,
    T_GLOBAL, T_DIM, T_LET, T_INPUT,
    T_MOD, T_AND, T_OR, T_XOR, T_NOT,
    T_PI, T_SWAP,
    T_ERROR,
    T_LONGSTRING
} TokType;

typedef struct {
    TokType type;
    double  num;
    char    text[1024];
    int     line;
} Token;

typedef struct {
    bs_State*   S;
    const char* src;
    int         pos;
    int         line;
    Token       cur;
    Token       next;
    int         has_next;
} Lexer;

void    lex_init   (Lexer* L, bs_State* S, const char* src);
void    lex_advance(Lexer* L);
TokType lex_peek   (Lexer* L);

#endif