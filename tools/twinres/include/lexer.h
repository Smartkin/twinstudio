#ifndef TR_LEXER_H
#define TR_LEXER_H

#include <stdint.h>

#include "string_view/string_view.h"

typedef enum {
    TwinRes_TokEof,

    TwinRes_TokSemicolon, // ";"
    TwinRes_TokColon, // ":"
    TwinRes_TokComma, // ","

    TwinRes_TokHash, // "#"

    TwinRes_TokLeftParen, // "("
    TwinRes_TokRightParen, // ")"

    TwinRes_TokOpenBracket, // "{"
    TwinRes_TokCloseBracket, // "}"

    TwinRes_TokAttribOpen, // "["
    TwinRes_TokAttribClose, // "]"

    TwinRes_TokAssign, // "="

    TwinRes_TokIdentifier, // any word

    TwinRes_TokInteger, // any 32-bit integer
    TwinRes_TokFloat,   // any 32-bit float
    TwinRes_TokString,  // any string
    TwinRes_TokBool, // any boolean
} TwinRes_LexerTokens;


typedef struct TwinRes_LexerToken {
    union {
        int64_t integer;
        float floating;
        TwinStudio_StringView string;
        bool boolean;
        void* data;
    };

    TwinRes_LexerTokens tokenType;
} TwinRes_LexerToken;


typedef struct TwinRes_Lexer {
    const char* strStart;
    char* curChar;
    uint32_t textIndex;
    uint32_t currentRow;
    uint32_t currentCol;
    TwinRes_LexerToken curTok;
} TwinRes_Lexer;


TwinRes_Lexer TwinRes_LexerCreate(char* string);
TwinRes_LexerToken TwinRes_LexerNextToken(TwinRes_Lexer* lexer);

#endif // TR_LEXER_H