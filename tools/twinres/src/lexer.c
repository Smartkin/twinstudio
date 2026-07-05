#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "lexer.h"
#include "math/math.h"
#include "string_view/string_view.h"

static const char reservedLexerChars[] = { '[', ']', '{', '}', '(', ')', '=', ',', ';', '\'', '"' };
static const char numericalChars[] = { '+', '-'};


static inline bool IsNullChar(char* ch)
{
    return ch == NULL || ch[0] == '\0';
}


static inline char AdvanceLexer(TwinRes_Lexer* lexer)
{
    if (IsNullChar(lexer->curChar))
    {
        return '\0';
    }

    if (*lexer->curChar == '\n')
    {
        lexer->currentRow = 0;
        lexer->currentCol++;
    }
    
    char curChar = *lexer->curChar;
    lexer->curChar++;
    lexer->textIndex++;
    lexer->currentRow++;

    return curChar;
}


static inline bool IsReservedChar(char ch)
{
    for (size_t i = 0; i < sizeof(reservedLexerChars); ++i)
    {
        if (ch == reservedLexerChars[i])
        {
            return true;
        }
    }

    return false;
}


static inline bool IsAllReservedChar(char ch)
{
    if (IsReservedChar(ch))
    {
        return true;
    }

    for (size_t i = 0; i < sizeof(numericalChars); ++i)
    {
        if (ch == numericalChars[i])
        {
            return true;
        }
    }

    return false;
}


static inline char PeekLexer(const TwinRes_Lexer* lexer)
{
    if (IsNullChar(lexer->curChar))
    {
        return '\0';
    }

    return lexer->curChar[0];
}


static inline TwinStudio_StringView AdvanceLexeme(TwinRes_Lexer* lexer)
{
    TwinStudio_StringView stringView = { .string = (lexer->curChar - 1), .length = 0 };
    if (IsNullChar(lexer->curChar))
    {
        stringView.length = 1;
        return stringView;
    }

    char curChar = lexer->curChar[0];
    while (curChar != '\0' && !isspace(curChar) && !IsAllReservedChar(curChar))
    {
        stringView.length++;

        curChar = AdvanceLexer(lexer);
    }

    if (stringView.length > 1)
    {
        lexer->curChar = lexer->curChar - 1;
        lexer->textIndex--;
    }
    else
    {
        stringView.length = 1;
    }
    
    return stringView;
}


static inline TwinStudio_StringView AdvanceLine(TwinRes_Lexer* lexer, char breakChar)
{
    TwinStudio_StringView stringView = { .string = lexer->curChar, .length = 0 };
    if (IsNullChar(lexer->curChar))
    {
        return stringView;
    }

    char trueBreakChar = breakChar;
    if (trueBreakChar == '\0')
    {
        trueBreakChar = '\n';
    }

    char curChar = lexer->curChar[0];
    while (curChar != '\0' && curChar != trueBreakChar)
    {
        stringView.length++;

        curChar = AdvanceLexer(lexer);
    }

    // Do not include the 2nd quote in the string
    if (stringView.length > 0)
    {
        stringView.length--;
    }
    
    return stringView;
}


TwinRes_Lexer TwinRes_LexerCreate(char* string)
{
    TwinRes_Lexer lexer = { .strStart = string, .curChar = string };
    lexer.curTok = TwinRes_LexerNextToken(&lexer);
    return lexer;
}


static bool IsIdentifier(TwinStudio_StringView str1, TwinStudio_StringView str2)
{
    return strncmp(str1.string, str2.string, TwinStudio_MinUInt(str1.length, str2.length)) == 0;
}


TwinRes_LexerToken TwinRes_LexerNextToken(TwinRes_Lexer* lexer)
{
    char curChar = '\0';
    char peekChar = '\0';
    TwinRes_LexerToken resultToken = { .tokenType = TwinRes_TokEof };

    while ((curChar = AdvanceLexer(lexer)) != '\0')
    {
        peekChar = PeekLexer(lexer);

        if (isspace(curChar))
        {
            continue;
        }

        if (curChar == '/' && peekChar == '/')
        {
            AdvanceLexer(lexer);
            AdvanceLine(lexer, '\0');
            continue;
        }

        if ((isdigit(curChar) || (curChar == '-' && isdigit(peekChar)) || (curChar == '+' && isdigit(peekChar))))
        {
            TwinStudio_StringView numString = AdvanceLexeme(lexer);
            bool isFloat = false;
            if (!IsNullChar(lexer->curChar) && lexer->curChar[0] == '.')
            {
                // Append the . and all the numbers after
                curChar = AdvanceLexer(lexer);
                numString.length += AdvanceLexeme(lexer).length + 1;
                isFloat = true;
            }

            if (isFloat)
            {
                resultToken.tokenType = TwinRes_TokFloat;
                resultToken.floating = strtof(numString.string, NULL);
            }
            else
            {
                resultToken.tokenType = TwinRes_TokInteger;
                resultToken.integer = strtol(numString.string, NULL, 0);
            }

            break;
        }

        if (curChar == '\'' || curChar == '"')
        {
            resultToken.tokenType = TwinRes_TokString;
            resultToken.string = AdvanceLine(lexer, curChar);
            break;
        }
        
        switch (curChar) {
            case '[':
                resultToken.tokenType = TwinRes_TokAttribOpen;
                break;
            case ']':
                resultToken.tokenType = TwinRes_TokAttribClose;
                break;
            case '{':
                resultToken.tokenType = TwinRes_TokOpenBracket;
                break;
            case '}':
                resultToken.tokenType = TwinRes_TokCloseBracket;
                break;
            case '=':
                resultToken.tokenType = TwinRes_TokAssign;
                break;
            case ';':
                resultToken.tokenType = TwinRes_TokSemicolon;
                break;
            case ':':
                resultToken.tokenType = TwinRes_TokColon;
                break;
            case ',':
                resultToken.tokenType = TwinRes_TokComma;
                break;
            case '#':
                resultToken.tokenType = TwinRes_TokHash;
                break;
            case '(':
                resultToken.tokenType = TwinRes_TokLeftParen;
                break;
            case ')':
                resultToken.tokenType = TwinRes_TokRightParen;
                break;
        }

        if (resultToken.tokenType == TwinRes_TokEof)
        {
            resultToken.string = AdvanceLexeme(lexer);
            if (resultToken.string.length > 0)
            {
                resultToken.tokenType = TwinRes_TokIdentifier;
                if (IsIdentifier(resultToken.string, TS_STRING_VIEW("false")) || IsIdentifier(resultToken.string, TS_STRING_VIEW("true")))
                {
                    resultToken.tokenType = TwinRes_TokBool;
                    resultToken.boolean = IsIdentifier(resultToken.string, TS_STRING_VIEW("true"));
                }
            }
        }

        if (resultToken.tokenType != TwinRes_TokEof)
        {
            break;
        }
    }

    lexer->curTok = resultToken;
    return resultToken;
}