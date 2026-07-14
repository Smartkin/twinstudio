#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "lexer.h"
#include "memory/memory.h"
#include "parser.h"
#include "math/math.h"
#include "string_view/string_view.h"
#include "variant/variant.h"


#define CREATE_ATTRIBUTE(attribName, type, initialValue) (TwinRes_AttributeInfo) { .name = TS_STRING_VIEW(#attribName), .data = __TS_VARIANT_CREATE_##type((initialValue)) }


static const uint32_t maxStructFields = 128;
static const uint32_t maxEnums = 256;
static const uint32_t maxAttributeArgs = 16;
static const uint32_t maxAstNodes = UINT16_MAX;
static const size_t maxResourceFileSize = 1 * 1024 * 1024; // max 1MB size of a single output file
static const uint32_t maxErrorStringLength = 1024;
static const char assetIdentifier[] = "asset";
static const char enumIdentifier[] = "enum";

static const char autoGenFileStartTemplate[] = "// This file was auto generated with TwinRes tool. DO NOT EDIT\n\n\n";

static const char structHeaderFileStartTemplate[] =
"#ifndef AUTO_TR_STRUCT_HEADER_%s_H\n"
"#define AUTO_TR_STRUCT_HEADER_%s_H\n\n";

static const char structHeaderFileEndTemplate[] =
"#endif // AUTO_TR_STRUCT_HEADER_%s_H";

static const char structImplementationFileStartTemplate[] =
"#include \"%s\"\n\n";


static void AppendParserNode(TwinRes_ParserNodeList* nodeList, TwinRes_ParserNode node)
{
    assert(nodeList->length + 1 <= nodeList->capacity);

    nodeList->nodes[nodeList->length] = node;
    nodeList->length++;
}


static TwinRes_AttributeArgumentsList* CreateAttributeArgumentList(TwinStudio_Arena* arena, size_t capacity)
{
    TwinRes_AttributeArgumentsList* argsList = TwinStudio_ArenaAlloc(arena, sizeof(TwinRes_AttributeArgumentsList) + (sizeof *argsList->arguments) * capacity);
    argsList->length = 0;
    argsList->capacity = capacity;

    return argsList;
}


static TwinRes_ParserNodeList* CreateNodeList(TwinStudio_Arena* arena, size_t capacity)
{
    TwinRes_ParserNodeList* nodeList = TwinStudio_ArenaAlloc(arena, sizeof(TwinRes_ParserNodeList) + (sizeof *nodeList->nodes) * capacity);
    nodeList->length = 0;
    nodeList->capacity = capacity;

    return nodeList;
}


static TwinRes_ParserNode GetEmptyNode()
{
    static TwinRes_ParserNode emptyNode = { .type = TwinRes_NodeEmpty };
    return emptyNode;
}


static TwinRes_ParserNode CreateNode(TwinRes_NodeTypes type, void* data)
{
    return (TwinRes_ParserNode) { .type = type, .data = data };
}


static TwinRes_ParserNode CreateNodeWithInt(TwinRes_NodeTypes type, int32_t data)
{
    return (TwinRes_ParserNode) { .type = type, .integer = data };
}


static TwinRes_ParserNode CreateNodeWithFloat(TwinRes_NodeTypes type, float data)
{
    return (TwinRes_ParserNode) { .type = type, .floating = data };
}


static TwinRes_ParserNode CreateNodeWithString(TwinRes_NodeTypes type, TwinStudio_StringView data)
{
    return (TwinRes_ParserNode) { .type = type, .string = data };
}


static int WriteFile(TwinRes_GeneratedFile* genFile, char* buffer, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int amountWritten = vsprintf(buffer, format, args);
    va_end(args);

    if (amountWritten < 0)
    {
        fprintf(stderr, "Not enough space to keep writing. Increase the amount of available autogen space in config.\n");
        return 0;
    }

    genFile->length += amountWritten;
    return amountWritten;
}


static bool EatToken(TwinRes_Lexer* lexer, TwinRes_LexerTokens expectedType)
{
    if (lexer->curTok.tokenType != expectedType)
    {
        fprintf(stderr, "Unexpected token: %d Expected token: %d\n", lexer->curTok.tokenType, expectedType);
        return false;
    }

    TwinRes_LexerNextToken(lexer);
    return true;
}


static TwinRes_GeneratorOutput CreateOutput(const char* baseFileName)
{
    TwinRes_GeneratorOutput output;
    output.status = TwinRes_GeneratorSuccess;
    output.generatorArena = TwinStudio_CreateArena(maxResourceFileSize * TwinRes_MaxGenFiles * 5);
    // Generated struct files
    {
        char* headerSourceStr = TwinStudio_ArenaAlloc(&output.generatorArena, maxResourceFileSize);
        char* implSourceStr = TwinStudio_ArenaAlloc(&output.generatorArena, maxResourceFileSize);
        char* userImplSourceStr = TwinStudio_ArenaAlloc(&output.generatorArena, maxResourceFileSize);

        const size_t fileNameMaxSize = strlen(baseFileName) + 256;

        char* headerSourceFileName = TwinStudio_ArenaAlloc(&output.generatorArena, fileNameMaxSize);
        char* implSourceFileName = TwinStudio_ArenaAlloc(&output.generatorArena, fileNameMaxSize);
        char* userImplSourceFileName = TwinStudio_ArenaAlloc(&output.generatorArena, fileNameMaxSize);

        snprintf(headerSourceFileName, fileNameMaxSize, "auto_struct_%s.h", baseFileName);
        snprintf(implSourceFileName, fileNameMaxSize, "auto_struct_%s.c", baseFileName);
        snprintf(userImplSourceFileName, fileNameMaxSize, "struct_%s.c", baseFileName);

        output.structFiles[TwinRes_GenHeader].data = headerSourceStr;
        output.structFiles[TwinRes_GenHeader].length = 0;
        output.structFiles[TwinRes_GenHeader].fileName = headerSourceFileName;
        output.structFiles[TwinRes_GenImplementation].data = implSourceStr;
        output.structFiles[TwinRes_GenImplementation].length = 0;
        output.structFiles[TwinRes_GenImplementation].fileName = implSourceFileName;
        output.structFiles[TwinRes_UserImplementation].data = userImplSourceStr;
        output.structFiles[TwinRes_UserImplementation].length = 0;
        output.structFiles[TwinRes_UserImplementation].fileName = userImplSourceFileName;
    }

    // Generated UI files
    {
        char* headerSourceStr = TwinStudio_ArenaAlloc(&output.generatorArena, maxResourceFileSize);
        char* implSourceStr = TwinStudio_ArenaAlloc(&output.generatorArena, maxResourceFileSize);
        char* userImplSourceStr = TwinStudio_ArenaAlloc(&output.generatorArena, maxResourceFileSize);

        const size_t fileNameMaxSize = strlen(baseFileName) + 256;

        char* headerSourceFileName = TwinStudio_ArenaAlloc(&output.generatorArena, fileNameMaxSize);
        char* implSourceFileName = TwinStudio_ArenaAlloc(&output.generatorArena, fileNameMaxSize);
        char* userImplSourceFileName = TwinStudio_ArenaAlloc(&output.generatorArena, fileNameMaxSize);

        snprintf(headerSourceFileName, fileNameMaxSize, "auto_ui_%s.h", baseFileName);
        snprintf(implSourceFileName, fileNameMaxSize, "auto_ui_%s.c", baseFileName);
        snprintf(userImplSourceFileName, fileNameMaxSize, "ui_%s.c", baseFileName);

        output.uiFiles[TwinRes_GenHeader].data = headerSourceStr;
        output.uiFiles[TwinRes_GenHeader].length = 0;
        output.uiFiles[TwinRes_GenHeader].fileName = headerSourceFileName;
        output.uiFiles[TwinRes_GenImplementation].data = implSourceStr;
        output.uiFiles[TwinRes_GenImplementation].length = 0;
        output.uiFiles[TwinRes_GenImplementation].fileName = implSourceFileName;
        output.uiFiles[TwinRes_UserImplementation].data = userImplSourceStr;
        output.uiFiles[TwinRes_UserImplementation].length = 0;
        output.uiFiles[TwinRes_UserImplementation].fileName = userImplSourceFileName;
    }


    output.errorString = TwinStudio_ArenaAlloc(&output.generatorArena, maxErrorStringLength);

    TwinRes_ParserNode* node = TwinStudio_ArenaAlloc(&output.generatorArena, sizeof(TwinRes_ParserNode));
    node->type = TwinRes_NodeList;
    node->data = CreateNodeList(&output.generatorArena, maxAstNodes);

    output.ast = node;

    return output;
}


static bool IsIdentifier(TwinStudio_StringView str1, TwinStudio_StringView str2)
{
    return strncmp(str1.string, str2.string, TwinStudio_MinUInt(str1.length, str2.length)) == 0;
}


static TwinRes_ParserNode LiteralNode(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    TwinStudio_Variant* variant = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinStudio_Variant));
    bool failed = false;
    switch (lexer->curTok.tokenType) {
        case TwinRes_TokInteger:
            *variant = TS_VARIANT_CREATE(int64_t, lexer->curTok.integer);
            EatToken(lexer, TwinRes_TokInteger);
            break;
        case TwinRes_TokFloat:
            *variant = TS_VARIANT_CREATE(float, lexer->curTok.floating);
            EatToken(lexer, TwinRes_TokFloat);
            break;
        case TwinRes_TokString:
            *variant = TS_VARIANT_CREATE(TwinStudio_StringView, lexer->curTok.string);
            EatToken(lexer, TwinRes_TokString);
            break;
        case TwinRes_TokBool:
            *variant = TS_VARIANT_CREATE(bool, lexer->curTok.boolean);
            EatToken(lexer, TwinRes_TokBool);
            break;
        case TwinRes_TokOpenBracket:
        {
            const char* objDefStart = lexer->curChar - 1;
            const uint32_t textIdx = lexer->textIndex - 1;
            EatToken(lexer, TwinRes_TokOpenBracket);
            while (lexer->curTok.tokenType != TwinRes_TokCloseBracket)
            {
                TwinRes_LexerNextToken(lexer);
            }
            const uint32_t defLength = lexer->textIndex - textIdx;
            EatToken(lexer, TwinRes_TokCloseBracket);
            const TwinStudio_StringView resDefString = (TwinStudio_StringView) { .string = objDefStart, .length = defLength, .isDynamicallyAllocated = false };
            *variant = TS_VARIANT_CREATE(TwinStudio_StringView, resDefString);
            variant->type = TwinStudio_VariantObject;
            break;
        }
        case TwinRes_TokIdentifier:
            *variant = TS_VARIANT_CREATE(TwinStudio_StringView, lexer->curTok.string);
            variant->type = TwinStudio_VariantEnum;
            EatToken(lexer, TwinRes_TokIdentifier);
            break;
        default:
            output->status = TwinRes_GeneratorFail;
            snprintf(output->errorString, maxErrorStringLength, "Unsupported literal token %d", lexer->curTok.tokenType);
            failed = true;
            break;
    }

    if (failed)
    {
        return GetEmptyNode();
    }

    return CreateNode(TwinRes_NodeLiteral, variant);
}


static TwinRes_ParserNode IncludeNode(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    if (!EatToken(lexer, TwinRes_TokHash))
    {
        return GetEmptyNode();
    }

    if (lexer->curTok.tokenType != TwinRes_TokIdentifier)
    {
        return GetEmptyNode();
    }

    TwinRes_LexerToken includeTok = lexer->curTok;
    EatToken(lexer, TwinRes_TokIdentifier);
    if (!IsIdentifier(includeTok.string, TS_STRING_VIEW("include")))
    {
        return GetEmptyNode();
    }

    if (lexer->curTok.tokenType != TwinRes_TokString)
    {
        return GetEmptyNode();
    }

    TwinRes_LexerToken stringTok = lexer->curTok;
    EatToken(lexer, TwinRes_TokString);

    return CreateNodeWithString(TwinRes_NodeInclude, stringTok.string);
}


static TwinRes_AttributeArgumentsList* GetArgumentList(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    TwinRes_AttributeArgumentsList* argsList = CreateAttributeArgumentList(&output->generatorArena, maxAttributeArgs);

    if (lexer->curTok.tokenType != TwinRes_TokLeftParen)
    {
        return argsList;
    }

    EatToken(lexer, TwinRes_TokLeftParen);
    while (lexer->curTok.tokenType != TwinRes_TokRightParen)
    {
        if (argsList->length >= argsList->capacity)
        {
            LiteralNode(output, lexer);
        }
        else
        {
            argsList->arguments[argsList->length++] = *(TwinStudio_Variant*)LiteralNode(output, lexer).data;
        }

        if (lexer->curTok.tokenType == TwinRes_TokComma)
        {
            EatToken(lexer, TwinRes_TokComma);
        }
    }

    EatToken(lexer, TwinRes_TokRightParen);

    return argsList;
}


typedef bool (*StructFieldAttributeHandler)(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* attribArguments);


static bool TryHandleStructFieldAttributeWrapper(const char* attribName, TwinStudio_StringView attemptedAttrib, TwinRes_AttributeArgumentsList* arguments, TwinRes_StructFieldGeneratorOptions* options, StructFieldAttributeHandler handler)
{
    if (strncmp(attribName, attemptedAttrib.string, TwinStudio_MinUInt(strlen(attribName), attemptedAttrib.length)) == 0)
    {
        return handler(options, arguments);
    }

    return false;
}


static bool HandleFieldResource(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 1)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantString)
    {
        return false;
    }
    
    TwinStudio_VariantSetBool(&options->resource.data, true);
    options->resource.arguments = arguments->arguments;
    options->resource.argumentLength = arguments->length;
    return true;
}


static bool HandleFieldCaption(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 1)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantString)
    {
        return false;
    }
    
    TwinStudio_VariantSetString(&options->caption.data, TS_VARIANT_GET(TwinStudio_StringView, arguments->arguments[0]));
    return true;
}


static bool HandleFieldVisibleIf(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 1)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantString)
    {
        return false;
    }
    
    TwinStudio_VariantSetString(&options->visibleIf.data, TS_VARIANT_GET(TwinStudio_StringView, arguments->arguments[0]));
    return true;
}


static bool HandleFieldWriteIf(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 1)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantString)
    {
        return false;
    }
    
    TwinStudio_VariantSetBool(&options->writeIf.data, true);
    options->writeIf.argumentLength = 1;
    options->writeIf.arguments = arguments->arguments;
    return true;
}


static bool HandleFieldNoJsonSerialization(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->excludeFromJson.data, true);
    return true;
}


static bool HandleFieldNoBinSerialization(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->excludeFromBin.data, true);
    return true;
}


static bool HandleFieldIsEnum(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->isEnum.data, true);
    return true;
}


static bool HandleFieldBlob(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->excludeFromJson.data, true);
    TwinStudio_VariantSetBool(&options->blob.data, true);
    return true;
}


static bool HandleFieldAlign(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 2)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantInt64 || arguments->arguments[1].type != TwinStudio_VariantInt64)
    {
        return false;
    }

    TwinStudio_VariantSetInt64(&options->align.data, arguments->arguments[0].storage.integer);
    options->align.argumentLength = 1;
    options->align.arguments = arguments->arguments + 1;
    return true;
}


static bool HandleFieldNoLength(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->noLength.data, true);
    return true;
}


static bool HandleFieldToBigEndian(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->toBigEndian.data, true);
    return true;
}


static bool HandleFieldLength(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 1)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantInt64 && arguments->arguments[0].type != TwinStudio_VariantEnum && arguments->arguments[0].type != TwinStudio_VariantString)
    {
        return false;
    }

    TwinStudio_VariantSetBool(&options->length.data, true);
    options->length.argumentLength = 1;
    options->length.arguments = arguments->arguments;
    return true;
}


static bool HandleFieldLinkResource(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 1)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantString)
    {
        return false;
    }
    
    TwinStudio_VariantSetBool(&options->linkResource.data, true);
    options->linkResource.argumentLength = 1;
    options->linkResource.arguments = arguments->arguments;
    return true;
}


static bool HandleFieldVoidRead(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->voidRead.data, true);
    return true;
}


static bool HandleFieldConstructor(TwinRes_StructFieldGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->constructor.data, true);
    options->constructor.argumentLength = arguments->length;
    options->constructor.arguments = arguments->arguments;
    return true;
}


static void HandleFieldAttribute(TwinStudio_StringView attribName, TwinRes_AttributeArgumentsList* arguments, TwinRes_StructFieldGeneratorOptions* options)
{
    if (TryHandleStructFieldAttributeWrapper("caption", attribName, arguments, options, HandleFieldCaption)) { return; }
    if (TryHandleStructFieldAttributeWrapper("visible_if", attribName, arguments, options, HandleFieldVisibleIf)) { return; }
    if (TryHandleStructFieldAttributeWrapper("write_if", attribName, arguments, options, HandleFieldWriteIf)) { return; }
    if (TryHandleStructFieldAttributeWrapper("no_bin", attribName, arguments, options, HandleFieldNoBinSerialization)) { return; }
    if (TryHandleStructFieldAttributeWrapper("no_json", attribName, arguments, options, HandleFieldNoJsonSerialization)) { return; }
    if (TryHandleStructFieldAttributeWrapper("is_enum", attribName, arguments, options, HandleFieldIsEnum)) { return; }
    if (TryHandleStructFieldAttributeWrapper("resource", attribName, arguments, options, HandleFieldResource)) { return; }
    if (TryHandleStructFieldAttributeWrapper("blob", attribName, arguments, options, HandleFieldBlob)) { return; }
    if (TryHandleStructFieldAttributeWrapper("align", attribName, arguments, options, HandleFieldAlign)) { return; }
    if (TryHandleStructFieldAttributeWrapper("no_length", attribName, arguments, options, HandleFieldNoLength)) { return; }
    if (TryHandleStructFieldAttributeWrapper("length", attribName, arguments, options, HandleFieldLength)) { return; }
    if (TryHandleStructFieldAttributeWrapper("big_endian", attribName, arguments, options, HandleFieldToBigEndian)) { return; }
    if (TryHandleStructFieldAttributeWrapper("link_resource", attribName, arguments, options, HandleFieldLinkResource)) { return; }
    if (TryHandleStructFieldAttributeWrapper("void_read", attribName, arguments, options, HandleFieldVoidRead)) { return; }
    if (TryHandleStructFieldAttributeWrapper("constructor", attribName, arguments, options, HandleFieldConstructor)) { return; }
    fprintf(stderr, "WARNING: Skipped unknown or malformed struct field attribute "TS_VIEW_FORMAT"\n", TS_VIEW_ARG(attribName));
}


static inline TwinRes_StructFieldGeneratorOptions GetDefaultFieldOptions()
{
    return (TwinRes_StructFieldGeneratorOptions) {
        .caption = CREATE_ATTRIBUTE(caption, TwinStudio_StringView, TS_STRING_VIEW("")),
        .visibleIf = CREATE_ATTRIBUTE(visible_if, TwinStudio_StringView, TS_STRING_VIEW("")),
        .writeIf = CREATE_ATTRIBUTE(write_if, bool, false),
        .excludeFromBin = CREATE_ATTRIBUTE(no_bin, bool, false),
        .excludeFromJson = CREATE_ATTRIBUTE(no_json, bool, false),
        .isEnum = CREATE_ATTRIBUTE(is_enum, bool, false),
        .resource = CREATE_ATTRIBUTE(resource, bool, false),
        .noLength = CREATE_ATTRIBUTE(no_length, bool, false),
        .length = CREATE_ATTRIBUTE(length, bool, false),
        .align = CREATE_ATTRIBUTE(align, int64_t, 0),
        .blob = CREATE_ATTRIBUTE(blob, bool, false),
        .toBigEndian = CREATE_ATTRIBUTE(big_endian, bool, false),
        .linkResource = CREATE_ATTRIBUTE(link_resource, bool, false),
        .voidRead = CREATE_ATTRIBUTE(void_read, bool, false),
        .constructor = CREATE_ATTRIBUTE(constructor, bool, false),
    };
}


static TwinRes_StructFieldGeneratorOptions GetFieldOptions(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    TwinRes_StructFieldGeneratorOptions result = GetDefaultFieldOptions();

    while(lexer->curTok.tokenType == TwinRes_TokAttribOpen)
    {
        EatToken(lexer, TwinRes_TokAttribOpen);
        TwinRes_LexerToken tok = lexer->curTok;
        if (!EatToken(lexer, TwinRes_TokIdentifier))
        {
            return result;
        }

        TwinRes_AttributeArgumentsList* arguments = GetArgumentList(output, lexer);
        TwinStudio_StringView attributeName = tok.string;
        HandleFieldAttribute(attributeName, arguments, &result);
        if (!EatToken(lexer, TwinRes_TokAttribClose))
        {
            return result;
        }
    }

    return result;
}


static inline TwinStudio_VariantType GetFieldType(const TwinStudio_StringView fieldTypeName)
{
    TwinStudio_VariantType result = TwinStudio_VariantNull;

    if (TwinStudio_StringContains(&fieldTypeName, '*'))
    {
        result |= TwinStudio_VariantArray;
    }

    if (strncmp("bool", fieldTypeName.string, sizeof("bool") - 1) == 0)
    {
        result |= TwinStudio_VariantBool;
    }
    else if (strncmp("float", fieldTypeName.string, sizeof("float") - 1) == 0)
    {
        result |= TwinStudio_VariantFloat;
    }
    else if (strncmp("TwinStudio_StringView", fieldTypeName.string, sizeof("TwinStudio_StringView") - 1) == 0)
    {
        result |= TwinStudio_VariantString;
    }
    else if (strncmp("uint8_t", fieldTypeName.string, sizeof("uint8_t") - 1) == 0)
    {
        result |= TwinStudio_VariantUInt8;
    }
    else if (strncmp("int8_t", fieldTypeName.string, sizeof("int8_t") - 1) == 0)
    {
        result |= TwinStudio_VariantInt8;
    }
    else if (strncmp("uint16_t", fieldTypeName.string, sizeof("uint16_t") - 1) == 0)
    {
        result |= TwinStudio_VariantUInt16;
    }
    else if (strncmp("int16_t", fieldTypeName.string, sizeof("int16_t") - 1) == 0)
    {
        result |= TwinStudio_VariantInt16;
    }
    else if (strncmp("int", fieldTypeName.string, sizeof("int") - 1) == 0
        || strncmp("int32_t", fieldTypeName.string, sizeof("int32_t") - 1) == 0)
    {
        result |= TwinStudio_VariantInt32;
    }
    else if (strncmp("uint32_t", fieldTypeName.string, sizeof("uint32_t") - 1) == 0)
    {
        result |= TwinStudio_VariantUInt32;
    }
    else if (strncmp("int64_t", fieldTypeName.string, sizeof("int64_t") - 1) == 0)
    {
        result |= TwinStudio_VariantInt64;
    }
    else if (strncmp("uint64_t", fieldTypeName.string, sizeof("uint64_t") - 1) == 0)
    {
        result |= TwinStudio_VariantUInt64;
    }
    else if (strncmp("char", fieldTypeName.string, sizeof("char") - 1) == 0)
    {
        result |= TwinStudio_VariantChar;
    }
    else
    {
        result |= TwinStudio_VariantObject;
    }

    return result;
}


static TwinRes_ParserNode StructFieldNode(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    TwinRes_StructFieldGeneratorOptions options = GetFieldOptions(output, lexer);

    if (lexer->curTok.tokenType != TwinRes_TokIdentifier)
    {
        return GetEmptyNode();
    }

    // Optional const type qualifier
    const bool hasQualifier = IsIdentifier(lexer->curTok.string, TS_STRING_VIEW("const"));

    TwinStudio_StringView fieldTypeName = lexer->curTok.string;
    TwinStudio_VariantType fieldType = TwinStudio_VariantObject;
    if (!EatToken(lexer, TwinRes_TokIdentifier))
    {
        return GetEmptyNode();
    }

    if (hasQualifier)
    {
        const TwinStudio_StringView actualFieldType = lexer->curTok.string;
        if (!EatToken(lexer, TwinRes_TokIdentifier))
        {
            return GetEmptyNode();
        }

        fieldType = GetFieldType(actualFieldType);
        // Append actual type name to the qualifier and space
        fieldTypeName.length += actualFieldType.length + 1;
    }
    else
    {
        fieldType = GetFieldType(fieldTypeName);
    }

    const TwinStudio_StringView fieldName = lexer->curTok.string;
    if (!EatToken(lexer, TwinRes_TokIdentifier))
    {
        return GetEmptyNode();
    }

    // Optional bit field specification
    int32_t bitfieldValue = -1;
    if (lexer->curTok.tokenType == TwinRes_TokColon)
    {
        EatToken(lexer, TwinRes_TokColon);

        TwinRes_LexerToken tok = lexer->curTok;
        if (!EatToken(lexer, TwinRes_TokInteger))
        {
            return GetEmptyNode();
        }

        bitfieldValue = tok.integer;
    }

    // Optional default value definition
    TwinStudio_Variant variant = TS_VARIANT_CREATE(object, NULL);
    variant.type = TwinStudio_VariantNull;
    if (lexer->curTok.tokenType == TwinRes_TokAssign)
    {
        EatToken(lexer, TwinRes_TokAssign);
        variant = *(TwinStudio_Variant*)LiteralNode(output, lexer).data;
    }

    if (!EatToken(lexer, TwinRes_TokSemicolon))
    {
        return GetEmptyNode();
    }

    TwinRes_ParserStructFieldDefinition* fieldNode = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinRes_ParserStructFieldDefinition));
    fieldNode->name = fieldName;
    if ((fieldType & TwinStudio_VariantArray) != 0)
    {
        fieldTypeName.length--;
    }
    fieldNode->type = fieldTypeName;
    fieldNode->options = options;
    fieldNode->bitfield = bitfieldValue;
    fieldNode->defaultValue = variant;
    fieldNode->isConst = hasQualifier;
    fieldNode->valueType = TS_VARIANT_GET(bool, options.isEnum.data) ? TwinStudio_VariantEnum : fieldType;

    return CreateNode(TwinRes_NodeFieldDefinition, fieldNode);
}


typedef bool (*StructAttributeHandler)(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* attribArguments);


static bool TryHandleStructAttributeWrapper(const char* attribName, TwinStudio_StringView attemptedAttrib, TwinRes_AttributeArgumentsList* arguments, TwinRes_StructGeneratorOptions* options, StructAttributeHandler handler)
{
    if (strncmp(attribName, attemptedAttrib.string, TwinStudio_MinUInt(strlen(attribName), attemptedAttrib.length)) == 0)
    {
        return handler(options, arguments);
    }

    return false;
}


static bool HandleStructCaption(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    if (arguments->length < 1)
    {
        return false;
    }

    if (arguments->arguments[0].type != TwinStudio_VariantString)
    {
        return false;
    }
    
    TwinStudio_VariantSetString(&options->caption.data, TS_VARIANT_GET(TwinStudio_StringView, arguments->arguments[0]));
    return true;
}


static bool HandleStructUseViewport(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->useViewport.data, true);
    return true;
}


static bool HandleStructUseCustomSerialization(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->useCustomSerialization.data, true);
    return true;
}


static bool HandleStructNoJsonSerialization(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->excludeFromJson.data, true);
    return true;
}


static bool HandleStructNoBinSerialization(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->excludeFromBin.data, true);
    return true;
}


static bool HandleStructNoReadSerialization(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->noRead.data, true);
    return true;
}


static bool HandleStructNoWriteSerialization(TwinRes_StructGeneratorOptions* options, TwinRes_AttributeArgumentsList* arguments)
{
    TwinStudio_VariantSetBool(&options->noWrite.data, true);
    return true;
}


static void HandleStructAttribute(TwinStudio_StringView attribName, TwinRes_AttributeArgumentsList* attribArguments, TwinRes_StructGeneratorOptions* options)
{
    if (TryHandleStructAttributeWrapper("custom_serialization", attribName, attribArguments, options, HandleStructUseCustomSerialization)) { return; }
    if (TryHandleStructAttributeWrapper("viewport", attribName, attribArguments, options, HandleStructUseViewport)) { return; }
    if (TryHandleStructAttributeWrapper("caption", attribName, attribArguments, options, HandleStructCaption)) { return; }
    if (TryHandleStructAttributeWrapper("no_json", attribName, attribArguments, options, HandleStructNoJsonSerialization)) { return; }
    if (TryHandleStructAttributeWrapper("no_bin", attribName, attribArguments, options, HandleStructNoBinSerialization)) { return; }
    if (TryHandleStructAttributeWrapper("no_read", attribName, attribArguments, options, HandleStructNoReadSerialization)) { return; }
    if (TryHandleStructAttributeWrapper("no_write", attribName, attribArguments, options, HandleStructNoWriteSerialization)) { return; }
    
    fprintf(stderr, "WARNING: Skipped unknown or malformed struct attribute "TS_VIEW_FORMAT"\n", TS_VIEW_ARG(attribName));
}


static TwinRes_StructGeneratorOptions GetStructOptions(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    TwinRes_StructGeneratorOptions result = { 
        .useCustomSerialization = CREATE_ATTRIBUTE(custom_serialization, bool, false),
        .useViewport = CREATE_ATTRIBUTE(viewport, bool, false),
        .caption = CREATE_ATTRIBUTE(caption, TwinStudio_StringView, TS_STRING_VIEW("")),
        .excludeFromBin = CREATE_ATTRIBUTE(no_bin, bool, false),
        .excludeFromJson = CREATE_ATTRIBUTE(no_json, bool, false),
        .noRead = CREATE_ATTRIBUTE(no_read, bool, false),
        .noWrite = CREATE_ATTRIBUTE(no_write, bool, false)
    };

    while(lexer->curTok.tokenType == TwinRes_TokAttribOpen)
    {
        EatToken(lexer, TwinRes_TokAttribOpen);
        TwinRes_LexerToken tok = lexer->curTok;
        if (!EatToken(lexer, TwinRes_TokIdentifier))
        {
            return result;
        }

        TwinRes_AttributeArgumentsList* arguments = GetArgumentList(output, lexer);
        TwinStudio_StringView attributeName = tok.string;
        HandleStructAttribute(attributeName, arguments, &result);
        if (!EatToken(lexer, TwinRes_TokAttribClose))
        {
            return result;
        }
    }

    return result;
}


static TwinRes_ParserNode StructDefinitionNode(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    TwinRes_StructGeneratorOptions options = GetStructOptions(output, lexer);

    bool isUnion = false;
    if (lexer->curTok.tokenType == TwinRes_TokIdentifier && IsIdentifier(lexer->curTok.string, TS_STRING_VIEW("union")))
    {
        isUnion = true;
    }

    if (!EatToken(lexer, TwinRes_TokIdentifier))
    {
        return GetEmptyNode();
    }

    const TwinStudio_StringView structName = lexer->curTok.string;
    EatToken(lexer, TwinRes_TokIdentifier);

    if (!EatToken(lexer, TwinRes_TokOpenBracket))
    {
        return GetEmptyNode();
    }

    TwinRes_ParserStructDefinition* structDefinition = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinRes_ParserStructDefinition));
    structDefinition->structName = structName;
    structDefinition->options = options;
    structDefinition->fields = CreateNodeList(&output->generatorArena, maxStructFields);
    structDefinition->isUnion = isUnion;

    // Parse fields
    while (lexer->curTok.tokenType != TwinRes_TokCloseBracket)
    {
        if (lexer->curTok.tokenType == TwinRes_TokIdentifier)
        {
            if (IsIdentifier(lexer->curTok.string, TS_STRING_VIEW("union")) || IsIdentifier(lexer->curTok.string, TS_STRING_VIEW("struct")))
            {
                TwinRes_ParserNode innerStructNode = StructDefinitionNode(output, lexer);
                if (innerStructNode.type == TwinRes_NodeEmpty)
                {
                    continue;
                }

                if (!EatToken(lexer, TwinRes_TokSemicolon))
                {
                    return GetEmptyNode();
                }

                AppendParserNode(structDefinition->fields, innerStructNode);
                continue;
            }
        }

        TwinRes_ParserNode fieldNode = StructFieldNode(output, lexer);
        if (fieldNode.type == TwinRes_NodeEmpty)
        {
            continue;
        }

        TwinRes_ParserStructFieldDefinition* fieldDef = fieldNode.data;
        if (TS_VARIANT_GET(bool, fieldDef->options.resource.data))
        {
            // Generate resource path field
            {
                TwinRes_StructFieldGeneratorOptions options = GetDefaultFieldOptions();
                TwinStudio_VariantSetBool(&options.excludeFromBin.data, true);

                char resourceFieldPathStr[512];
                snprintf(resourceFieldPathStr, 512, TS_VIEW_FORMAT"Path", TS_VIEW_ARG(fieldDef->name));

                TwinRes_ParserStructFieldDefinition* fieldNode = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinRes_ParserStructFieldDefinition));
                fieldNode->name = TwinStudio_CopyFromCStringArena(&output->generatorArena, resourceFieldPathStr);
                fieldNode->type = TwinStudio_CopyFromCStringArena(&output->generatorArena, "TwinStudio_StringView");
                fieldNode->options = options;
                fieldNode->valueType = TwinStudio_VariantString;

                AppendParserNode(structDefinition->fields, CreateNode(TwinRes_NodeFieldDefinition, fieldNode));
            }

            // Generate resource data field
            {
                TwinRes_StructFieldGeneratorOptions options = GetDefaultFieldOptions();
                TwinStudio_VariantSetBool(&options.excludeFromJson.data, true);
                options.length = fieldDef->options.length;

                TwinRes_ParserStructFieldDefinition* fieldNode = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinRes_ParserStructFieldDefinition));
                fieldNode->name = fieldDef->name;
                fieldNode->type = TS_VARIANT_GET(TwinStudio_StringView, fieldDef->options.resource.arguments[0]);
                fieldNode->options = options;
                fieldNode->valueType = TwinStudio_VariantObject;

                AppendParserNode(structDefinition->fields, CreateNode(TwinRes_NodeFieldDefinition, fieldNode));
            }

            continue;
        }

        if (TS_VARIANT_GET(bool, fieldDef->options.linkResource.data))
        {
            // Generate resource type
            {
                TwinRes_StructFieldGeneratorOptions options = GetDefaultFieldOptions();
                TwinStudio_VariantSetBool(&options.excludeFromBin.data, true);
                TwinStudio_VariantSetBool(&options.excludeFromJson.data, true);

                char resourceFieldPathStr[512];
                snprintf(resourceFieldPathStr, 512, TS_VIEW_FORMAT"LinkType", TS_VIEW_ARG(fieldDef->name));

                TwinRes_ParserStructFieldDefinition* fieldNode = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinRes_ParserStructFieldDefinition));
                fieldNode->name = TwinStudio_CopyFromCStringArena(&output->generatorArena, resourceFieldPathStr);
                fieldNode->type = TwinStudio_CopyFromCStringArena(&output->generatorArena, "TwinStudio_ResourceType");
                fieldNode->options = options;
                fieldNode->valueType = TwinStudio_VariantEnum;
                fieldNode->defaultValue.type = TwinStudio_VariantEnum;
                fieldNode->defaultValue.storage.string = TS_VARIANT_GET(TwinStudio_StringView, fieldDef->options.linkResource.arguments[0]);

                AppendParserNode(structDefinition->fields, CreateNode(TwinRes_NodeFieldDefinition, fieldNode));
            }
            
            // Generate resource link
            {
                TwinRes_StructFieldGeneratorOptions options = GetDefaultFieldOptions();
                TwinStudio_VariantSetBool(&options.excludeFromBin.data, true);

                char resourceFieldPathStr[512];
                snprintf(resourceFieldPathStr, 512, TS_VIEW_FORMAT"Link", TS_VIEW_ARG(fieldDef->name));

                TwinRes_ParserStructFieldDefinition* fieldNode = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinRes_ParserStructFieldDefinition));
                fieldNode->name = TwinStudio_CopyFromCStringArena(&output->generatorArena, resourceFieldPathStr);
                fieldNode->type = TwinStudio_CopyFromCStringArena(&output->generatorArena, "TwinStudio_StringView");
                fieldNode->options = options;
                fieldNode->valueType = TwinStudio_VariantString;
                if (fieldDef->valueType & TwinStudio_VariantArray)
                {
                    fieldNode->valueType |= TwinStudio_VariantArray;
                }

                AppendParserNode(structDefinition->fields, CreateNode(TwinRes_NodeFieldDefinition, fieldNode));
            }
        }

        AppendParserNode(structDefinition->fields, fieldNode);
    }

    EatToken(lexer, TwinRes_TokCloseBracket);

    return CreateNode(TwinRes_NodeStructDefinition, structDefinition);
}


static TwinRes_ParserNode EnumNode(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer, int32_t* curEnumIndex)
{
    const TwinStudio_StringView enumName = lexer->curTok.string;
    if (!EatToken(lexer, TwinRes_TokIdentifier))
    {
        return GetEmptyNode();
    }

    bool isCustomValue = false;
    if (lexer->curTok.tokenType == TwinRes_TokAssign)
    {
        EatToken(lexer, TwinRes_TokAssign);
        if (lexer->curTok.tokenType != TwinRes_TokInteger)
        {
            return GetEmptyNode();
        }

        *curEnumIndex = lexer->curTok.integer;
        EatToken(lexer, TwinRes_TokInteger);
        isCustomValue = true;
    }

    TwinRes_ParserEnumName* enumNameDef = TwinStudio_ArenaAlloc(&output->generatorArena, (sizeof *enumNameDef));
    enumNameDef->name = enumName;
    enumNameDef->isCustomValue = isCustomValue;
    enumNameDef->value = *curEnumIndex;

    return CreateNode(TwinRes_NodeEnumName, enumNameDef);
}


static TwinRes_ParserNode EnumDefinitionNode(TwinRes_GeneratorOutput* output, TwinRes_Lexer* lexer)
{
    if (!EatToken(lexer, TwinRes_TokIdentifier))
    {
        return GetEmptyNode();
    }

    const TwinStudio_StringView enumName = lexer->curTok.string;
    EatToken(lexer, TwinRes_TokIdentifier);

    if (!EatToken(lexer, TwinRes_TokOpenBracket))
    {
        return GetEmptyNode();
    }

    TwinRes_ParserEnumDefinition* enumDefinition = TwinStudio_ArenaAlloc(&output->generatorArena, sizeof(TwinRes_ParserEnumDefinition));
    enumDefinition->enumName = enumName;
    enumDefinition->enums = CreateNodeList(&output->generatorArena, maxEnums);

    int32_t enumIndex = 0;
    while (lexer->curTok.tokenType != TwinRes_TokCloseBracket)
    {
        TwinRes_ParserNode enumNode = EnumNode(output, lexer, &enumIndex);
        if (enumNode.type == TwinRes_NodeEmpty)
        {
            continue;
        }

        AppendParserNode(enumDefinition->enums, enumNode);
        enumIndex++;

        if (lexer->curTok.tokenType != TwinRes_TokCloseBracket)
        {
            EatToken(lexer, TwinRes_TokComma);
        }
    }

    EatToken(lexer, TwinRes_TokCloseBracket);

    return CreateNode(TwinRes_NodeEnumDefinition, enumDefinition);
}



static int GenerateInclude(TwinRes_ParserNode* node, TwinRes_GeneratedFile* genFile, char* buffer)
{
    assert(node->type == TwinRes_NodeInclude);
    static const char includeFormat[] = "#include \""TS_VIEW_FORMAT"\"\n";

    return WriteFile(genFile, buffer, includeFormat, TS_VIEW_ARG(node->string));
}


static int GenerateStructField(TwinRes_ParserNode* node, TwinRes_GeneratedFile* genFile, char* buffer)
{
    assert(node->type == TwinRes_NodeFieldDefinition);

    static const char structArrayFormat[] = "   "TS_VIEW_FORMAT"* "TS_VIEW_FORMAT";\n";
    static const char structFieldFormat[] = "   "TS_VIEW_FORMAT" "TS_VIEW_FORMAT";\n";
    static const char structBitFieldFormat[] = "   "TS_VIEW_FORMAT" "TS_VIEW_FORMAT" : %d;\n";

    TwinRes_ParserStructFieldDefinition* fieldDef = node->data;

    const TwinStudio_StringView fieldTypeName = fieldDef->type;
    const TwinStudio_StringView fieldName = fieldDef->name;
    const bool isArray = fieldDef->valueType & TwinStudio_VariantArray;

    if (isArray)
    {
        return WriteFile(genFile, buffer, structArrayFormat, TS_VIEW_ARG(fieldTypeName), TS_VIEW_ARG(fieldName));
    }

    if (fieldDef->bitfield > 0)
    {
        return WriteFile(genFile, buffer, structBitFieldFormat, TS_VIEW_ARG(fieldTypeName), TS_VIEW_ARG(fieldName), fieldDef->bitfield);
    }

    return WriteFile(genFile, buffer, structFieldFormat, TS_VIEW_ARG(fieldTypeName), TS_VIEW_ARG(fieldName));
}


static int GenerateStructFieldBinarySerialization(TwinRes_ParserNode* node, TwinRes_GeneratedFile* genFile, char* buffer)
{
    assert(node->type == TwinRes_NodeFieldDefinition);
    int amountWritten = 0;

    TwinRes_ParserStructFieldDefinition* structField = node->data;
    if (TS_VARIANT_GET(bool, structField->options.excludeFromBin.data))
    {
        return amountWritten;
    }

    char sizeFormat[1024] = "size";
    const bool isLengthDefined = TS_VARIANT_GET(bool, structField->options.length.data);
    if (isLengthDefined)
    {
        if (structField->options.length.arguments[0].type == TwinStudio_VariantInt64)
        {
            snprintf(sizeFormat, 1024, "%ld", TS_VARIANT_GET(int64_t, structField->options.length.arguments[0]));
        }
        else if (structField->options.length.arguments[0].type == TwinStudio_VariantString)
        {
            snprintf(sizeFormat, 1024, TS_VIEW_FORMAT, TS_VIEW_ARG(TS_VARIANT_GET(TwinStudio_StringView, structField->options.length.arguments[0])));
        }
        else
        {
            snprintf(sizeFormat, 1024, "source->"TS_VIEW_FORMAT, TS_VIEW_ARG(structField->options.length.arguments[0].storage.string));
        }
    }

    if (TS_VARIANT_GET(bool, structField->options.blob.data))
    {
        int fieldBodyWritten = WriteFile(genFile, buffer, "   TwinStudio_BinWriteBlob(serializer, source->"TS_VIEW_FORMAT", %s);\n", TS_VIEW_ARG(structField->name), sizeFormat);
        if (fieldBodyWritten == 0)
        {
            return 0;
        }
        amountWritten += fieldBodyWritten;
        buffer += fieldBodyWritten;
        return amountWritten;
    }

    if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
    {
        int writeIfWritten = WriteFile(genFile, buffer, "   if ("TS_VIEW_FORMAT") {\n", TS_VIEW_ARG(TS_VARIANT_GET(TwinStudio_StringView, structField->options.writeIf.arguments[0])));
        if (writeIfWritten == 0)
        {
            return 0;
        }
        amountWritten += writeIfWritten;
        buffer += writeIfWritten;
    }

    int fieldBodyWritten = 0;
    const bool isArray = structField->valueType & TwinStudio_VariantArray;
    const bool isInfArray = TS_VARIANT_GET(bool, structField->options.noLength.data);
    char* indexWriting = "";
    char* extraSpaces = "   ";
    if (isArray)
    {
        extraSpaces = "      ";
        if (!isLengthDefined)
        {
            if (!isInfArray)
            {
                int arrLenWritten = WriteFile(genFile, buffer, "   TwinStudio_BinWriteInt32(serializer, arrlen(source->"TS_VIEW_FORMAT"));\n", TS_VIEW_ARG(structField->name));
                if (arrLenWritten == 0)
                {
                    return 0;
                }
                amountWritten += arrLenWritten;
                buffer += arrLenWritten;
            }

            int arrForLoopStart = WriteFile(genFile, buffer, "   for(uint32_t i = 0; i < arrlen(source->"TS_VIEW_FORMAT"); ++i) {\n", TS_VIEW_ARG(structField->name));
            if (arrForLoopStart == 0)
            {
                return 0;
            }
            amountWritten += arrForLoopStart;
            buffer += arrForLoopStart;
        }
        else if (isLengthDefined)
        {
            int arrForLoopStart = WriteFile(genFile, buffer, "   for(uint32_t i = 0; i < %s; ++i) {\n", sizeFormat);
            if (arrForLoopStart == 0)
            {
                return 0;
            }
            amountWritten += arrForLoopStart;
            buffer += arrForLoopStart;
        }

        indexWriting = "[i]";
    }

    const bool isBigEndian = TS_VARIANT_GET(bool, structField->options.toBigEndian.data);
    const TwinStudio_VariantType valueType = structField->valueType & (~TwinStudio_VariantArray);
    switch (valueType)
    {
        case TwinStudio_VariantUInt8:
        case TwinStudio_VariantBool:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteUInt8(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantFloat:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteFloat(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantInt8:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteInt8(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantInt16:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteInt16(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantUInt16:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteUInt16(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantInt32:
        case TwinStudio_VariantEnum:
            if (isBigEndian)
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteInt32(serializer, ToBigEndian(source->"TS_VIEW_FORMAT"%s));\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            }
            else
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteInt32(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            }
            break;
        case TwinStudio_VariantUInt32:
            if (isBigEndian)
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteUInt32(serializer, ToBigEndian(source->"TS_VIEW_FORMAT"%s));\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            }
            else
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteUInt32(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            }
            break;
        case TwinStudio_VariantInt64:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteInt64(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantUInt64:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteUInt64(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantString:
            if (!isArray && isLengthDefined)
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "   TwinStudio_BinWriteChars(serializer, source->"TS_VIEW_FORMAT".string, %s);\n", TS_VIEW_ARG(structField->name), sizeFormat);
            }
            else
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteString(serializer, source->"TS_VIEW_FORMAT"%s, false);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            }
            break;
        case TwinStudio_VariantChar:
            fieldBodyWritten = WriteFile(genFile, buffer, "%sTwinStudio_BinWriteChar(serializer, source->"TS_VIEW_FORMAT"%s);\n", extraSpaces, TS_VIEW_ARG(structField->name), indexWriting);
            break;
        case TwinStudio_VariantObject:
            if (!isLengthDefined)
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "%s"TS_VIEW_FORMAT"BinSerialize(&source->"TS_VIEW_FORMAT"%s, serializer, arena, sizeof("TS_VIEW_FORMAT"), source);\n", extraSpaces, TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name), indexWriting, TS_VIEW_ARG(structField->type));
            }
            else
            {
                fieldBodyWritten = WriteFile(genFile, buffer, "%s"TS_VIEW_FORMAT"BinSerialize(&source->"TS_VIEW_FORMAT"%s, serializer, arena, %s, source);\n", extraSpaces, TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name), indexWriting, sizeFormat);
            }
            break;
        default:
            break;
    }

    if (fieldBodyWritten == 0)
    {
        return 0;
    }
    amountWritten += fieldBodyWritten;
    buffer += fieldBodyWritten;

    if (TS_VARIANT_GET(int64_t, structField->options.align.data) > 0)
    {
        int alignWritten = WriteFile(genFile, buffer, "%swhile(TwinStudio_BinGetStreamPosition(serializer) %% %d != 0) TwinStudio_BinWriteUInt8(serializer, %d);\n", extraSpaces, TS_VARIANT_GET(int64_t, structField->options.align.data), TS_VARIANT_GET(int64_t, structField->options.align.arguments[0]));
        if (alignWritten == 0)
        {
            return 0;
        }
        amountWritten += alignWritten;
        buffer += alignWritten;
    }

    if (isArray)
    {
        int arrEndWritten = WriteFile(genFile, buffer, "   }\n");
        if (arrEndWritten == 0)
        {
            return 0;
        }
        amountWritten += arrEndWritten;
        buffer += arrEndWritten;
    }

    if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
    {
        int writeIfWritten = WriteFile(genFile, buffer, "   }\n");
        if (writeIfWritten == 0)
        {
            return 0;
        }
        amountWritten += writeIfWritten;
        buffer += writeIfWritten;
    }

    return amountWritten;
}


static int GenerateStructFieldsBinarySerialization(const TwinRes_ParserStructDefinition* structDef, TwinRes_GeneratedFile* genFile, char* buffer)
{
    int amountWritten = 0;
    int fieldsAmount = structDef->fields->length;
    if (structDef->isUnion && fieldsAmount > 1)
    {
        fieldsAmount = 1;
    }

    for (uint32_t i = 0; i < fieldsAmount; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            const TwinRes_ParserStructDefinition* structDef = node->data;
            int innerStructWritten = GenerateStructFieldsBinarySerialization(structDef, genFile, buffer);
            amountWritten += innerStructWritten;
            buffer += innerStructWritten;
            continue;
        }

        int fieldWritten = GenerateStructFieldBinarySerialization(node, genFile, buffer);
        amountWritten += fieldWritten;
        buffer += fieldWritten;
    }

    return amountWritten;
}


static int GenerateStructFieldsBinaryDeserialization(const TwinRes_ParserStructDefinition* structDef, TwinRes_GeneratedFile* genFile, char* buffer)
{
    int amountWritten = 0;
    int fieldsAmount = structDef->fields->length;
    if (structDef->isUnion && fieldsAmount > 1)
    {
        fieldsAmount = 1;
    }

    for (uint32_t i = 0; i < fieldsAmount; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            TwinRes_ParserStructDefinition* structDef = node->data;
            int innerStructWritten = GenerateStructFieldsBinaryDeserialization(structDef, genFile, buffer);
            amountWritten += innerStructWritten;
            buffer += innerStructWritten;
            continue;
        }

        TwinRes_ParserStructFieldDefinition* structField = node->data;
        if (TS_VARIANT_GET(bool, structField->options.excludeFromBin.data) || structField->isConst)
        {
            continue;
        }

        char constructorBuffer[1024];
        constructorBuffer[0] = '\0';
        if (TS_VARIANT_GET(bool, structField->options.constructor.data) && structField->options.constructor.argumentLength > 0)
        {
            for (uint32_t j = 0; j < structField->options.constructor.argumentLength; ++j)
            {
                if (j > 0 && j < structField->options.constructor.argumentLength - 1)
                {
                    strcat(constructorBuffer, ", ");
                }

                char paramBuffer[128];
                paramBuffer[0] = '\0';
                snprintf(paramBuffer, 128, "target->"TS_VIEW_FORMAT, TS_VIEW_ARG(structField->options.constructor.arguments[j].storage.string));
                strcat(constructorBuffer, paramBuffer);
            }
        }

        char sizeFormat[1024] = "size";
        const bool isBigEndian = TS_VARIANT_GET(bool, structField->options.toBigEndian.data);
        const bool isLengthDefined = TS_VARIANT_GET(bool, structField->options.length.data);
        const bool isVoidRead = TS_VARIANT_GET(bool, structField->options.voidRead.data);
        if (isLengthDefined)
        {
            if (structField->options.length.arguments[0].type == TwinStudio_VariantInt64)
            {
                snprintf(sizeFormat, 1024, "%ld", TS_VARIANT_GET(int64_t, structField->options.length.arguments[0]));
            }
            else if (structField->options.length.arguments[0].type == TwinStudio_VariantString)
            {
                TwinStudio_StringView originalLength = TS_VARIANT_GET(TwinStudio_StringView, structField->options.length.arguments[0]);
                TwinStudio_StringView lengthCopy = TwinStudio_CopyFromCString(TwinStudio_GetCStringDynamic(&originalLength));
                char* replacement = NULL;
                char* currentSearch = lengthCopy.dynString;
                while ((replacement = strstr(currentSearch, "source")) != NULL)
                {
                    memcpy(replacement, "target", sizeof("target") - 1);
                    currentSearch = replacement;
                }
                snprintf(sizeFormat, 1024, TS_VIEW_FORMAT, TS_VIEW_ARG(lengthCopy));
                TwinStudio_FreeString(&lengthCopy);
            }
            else
            {
                snprintf(sizeFormat, 1024, "target->"TS_VIEW_FORMAT, TS_VIEW_ARG(structField->options.length.arguments[0].storage.string));
            }
        }

        if (TS_VARIANT_GET(bool, structField->options.blob.data))
        {
            int fieldBodyWritten = WriteFile(genFile, buffer, "   target->"TS_VIEW_FORMAT" = TwinStudio_BinReadBlob(deserializer, arena, %s);\n", TS_VIEW_ARG(structField->name), sizeFormat);
            if (fieldBodyWritten == 0)
            {
                return 0;
            }
            amountWritten += fieldBodyWritten;
            buffer += fieldBodyWritten;
            continue;
        }

        if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
        {
            TwinStudio_StringView originalCondition = TS_VARIANT_GET(TwinStudio_StringView, structField->options.writeIf.arguments[0]);
            TwinStudio_StringView conditionCopy = TwinStudio_CopyFromCString(TwinStudio_GetCStringDynamic(&originalCondition));
            char* replacement = NULL;
            char* currentSearch = conditionCopy.dynString;
            while ((replacement = strstr(currentSearch, "source")) != NULL)
            {
                memcpy(replacement, "target", sizeof("target") - 1);
                currentSearch = replacement;
            }
            int writeIfWritten = WriteFile(genFile, buffer, "   if ("TS_VIEW_FORMAT") {\n", TS_VIEW_ARG(conditionCopy));
            TwinStudio_FreeString(&conditionCopy);
            if (writeIfWritten == 0)
            {
                return 0;
            }
            amountWritten += writeIfWritten;
            buffer += writeIfWritten;
        }

        if (isVoidRead)
        {
            int fieldBodyWritten = WriteFile(genFile, buffer, "   TwinStudio_BinReadVoid(deserializer, %s);\n", sizeFormat);
            if (fieldBodyWritten == 0)
            {
                return 0;
            }
            amountWritten += fieldBodyWritten;
            buffer += fieldBodyWritten;
            goto finishWriteIf;
        }

        const TwinStudio_VariantType valueType = structField->valueType & (~TwinStudio_VariantArray);
        const bool isArray = structField->valueType & TwinStudio_VariantArray;
        const bool isInfArray = TS_VARIANT_GET(bool, structField->options.noLength.data);
        if (isArray)
        {
            if (!isInfArray)
            {
                if (!isLengthDefined)
                {
                    int arrLenWritten = WriteFile(genFile, buffer, "   uint32_t "TS_VIEW_FORMAT"Size = TwinStudio_BinReadInt32(deserializer);\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    if (arrLenWritten == 0)
                    {
                        return 0;
                    }
                    amountWritten += arrLenWritten;
                    buffer += arrLenWritten;

                    int arrForLoopStart = WriteFile(genFile, buffer, "   for(uint32_t i = 0; i < "TS_VIEW_FORMAT"Size; ++i) {\n", TS_VIEW_ARG(structField->name));
                    if (arrForLoopStart == 0)
                    {
                        return 0;
                    }
                    amountWritten += arrForLoopStart;
                    buffer += arrForLoopStart;
                }
                else
                {
                    int arrForLoopStart = WriteFile(genFile, buffer, "   arrsetcap(target->"TS_VIEW_FORMAT", %s); for(uint32_t i = 0; i < %s; ++i) {\n", TS_VIEW_ARG(structField->name), sizeFormat, sizeFormat);
                    if (arrForLoopStart == 0)
                    {
                        return 0;
                    }
                    amountWritten += arrForLoopStart;
                    buffer += arrForLoopStart;
                }
            }
            else
            {
                int arrForLoopStart = WriteFile(genFile, buffer, "   while(TwinStudio_BinGetStreamPosition(deserializer) < TwinStudio_BinGetStreamLength(deserializer)) {\n", TS_VIEW_ARG(structField->name));
                if (arrForLoopStart == 0)
                {
                    return 0;
                }
                amountWritten += arrForLoopStart;
                buffer += arrForLoopStart;
            }

            if (valueType != TwinStudio_VariantObject)
            {
                int prePutWritten = WriteFile(genFile, buffer, "      arrput(target->"TS_VIEW_FORMAT", ", TS_VIEW_ARG(structField->name));
                if (prePutWritten == 0)
                {
                    return 0;
                }
                amountWritten += prePutWritten;
                buffer += prePutWritten;
            }
        }
        
        int fieldBodyWritten = 0;
        char* readFunc = "";
        char readFuncBuffer[256];
        switch (valueType)
        {
            case TwinStudio_VariantInt8:
                readFunc = "TwinStudio_BinReadInt8(deserializer)";
                break;
            case TwinStudio_VariantUInt8:
            case TwinStudio_VariantBool:
                readFunc = "TwinStudio_BinReadUInt8(deserializer)";
                break;
            case TwinStudio_VariantFloat:
                readFunc = "TwinStudio_BinReadFloat(deserializer)";
                break;
            case TwinStudio_VariantInt16:
                readFunc = "TwinStudio_BinReadInt16(deserializer)";
                break;
            case TwinStudio_VariantUInt16:
                readFunc = "TwinStudio_BinReadUInt16(deserializer)";
                break;
            case TwinStudio_VariantInt32:
            case TwinStudio_VariantEnum:
                readFunc = "TwinStudio_BinReadInt32(deserializer)";
                break;
            case TwinStudio_VariantUInt32:
                readFunc = "TwinStudio_BinReadUInt32(deserializer)";
                break;
            case TwinStudio_VariantInt64:
                readFunc = "TwinStudio_BinReadInt64(deserializer)";
                break;
            case TwinStudio_VariantUInt64:
                readFunc = "TwinStudio_BinReadUInt64(deserializer)";
                break;
            case TwinStudio_VariantChar:
                readFunc = "TwinStudio_BinReadChar(deserializer)";
                break;
            case TwinStudio_VariantString:
                if (!isArray && isLengthDefined)
                {
                    sprintf(readFuncBuffer, "TwinStudio_CopyFromCString(TwinStudio_BinReadChars(deserializer, arena, %s))", sizeFormat);
                    readFunc = readFuncBuffer;
                }
                else
                {
                    readFunc = "TwinStudio_BinReadString(deserializer, arena)";
                }
                break;
            default:
                break;
        }
        if (isArray)
        {
            if (valueType != TwinStudio_VariantObject)
            {
                if (isBigEndian)
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "ToBigEndian(%s));\n", readFunc);
                }
                else
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "%s);\n", readFunc);
                }
            }
            else
            {
                if (isLengthDefined)
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "      "TS_VIEW_FORMAT" createdObj = "TS_VIEW_FORMAT"Create(%s); "TS_VIEW_FORMAT"BinDeserialize(&createdObj, deserializer, arena, %s, target); arrput(target->"TS_VIEW_FORMAT", createdObj);\n",
                        TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->type), constructorBuffer, TS_VIEW_ARG(structField->type), sizeFormat, TS_VIEW_ARG(structField->name));
                }
                else
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "      "TS_VIEW_FORMAT" createdObj = "TS_VIEW_FORMAT"Create(%s); "TS_VIEW_FORMAT"BinDeserialize(&createdObj, deserializer, arena, sizeof("TS_VIEW_FORMAT"), target); arrput(target->"TS_VIEW_FORMAT", createdObj);\n",
                        TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->type), constructorBuffer, TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name));
                }
            }
        }
        else
        {
            if (valueType != TwinStudio_VariantObject)
            {
                if (isBigEndian)
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   target->"TS_VIEW_FORMAT" = ToBigEndian(%s);\n", TS_VIEW_ARG(structField->name), readFunc);
                }
                else
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   target->"TS_VIEW_FORMAT" = %s;\n", TS_VIEW_ARG(structField->name), readFunc);
                }
            }
            else
            {
                if (isLengthDefined)
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   "TS_VIEW_FORMAT"BinDeserialize(&target->"TS_VIEW_FORMAT", deserializer, arena, %s, target);\n", TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name), sizeFormat);
                }
                else
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   "TS_VIEW_FORMAT"BinDeserialize(&target->"TS_VIEW_FORMAT", deserializer, arena, sizeof("TS_VIEW_FORMAT"), target);\n", TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->type));
                }
            }
        }
        if (fieldBodyWritten == 0)
        {
            return 0;
        }
        amountWritten += fieldBodyWritten;
        buffer += fieldBodyWritten;
        if (isArray)
        {
            int arrEndWritten = WriteFile(genFile, buffer, "   }\n");
            if (arrEndWritten == 0)
            {
                return 0;
            }
            amountWritten += arrEndWritten;
            buffer += arrEndWritten;
        }

finishWriteIf:
        if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
        {
            int writeIfWritten = WriteFile(genFile, buffer, "   }\n");
            if (writeIfWritten == 0)
            {
                return 0;
            }
            amountWritten += writeIfWritten;
            buffer += writeIfWritten;
        }
    }

    return amountWritten;
}


static int GenerateStructFieldsJsonSerialization(const TwinRes_ParserStructDefinition* structDef, TwinRes_GeneratedFile* genFile, char* buffer)
{
    int amountWritten = 0;
    int fieldsAmount = structDef->fields->length;
    if (structDef->isUnion && fieldsAmount > 1)
    {
        fieldsAmount = 1;
    }

    for (uint32_t i = 0; i < fieldsAmount; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            TwinRes_ParserStructDefinition* structDef = node->data;
            int innerStructWritten = GenerateStructFieldsJsonSerialization(structDef, genFile, buffer);
            amountWritten += innerStructWritten;
            buffer += innerStructWritten;
            continue;
        }

        TwinRes_ParserStructFieldDefinition* structField = node->data;
        if (TS_VARIANT_GET(bool, structField->options.excludeFromJson.data))
        {
            continue;
        }

        if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
        {
            int writeIfWritten = WriteFile(genFile, buffer, "   if ("TS_VIEW_FORMAT") {\n", TS_VIEW_ARG(TS_VARIANT_GET(TwinStudio_StringView, structField->options.writeIf.arguments[0])));
            if (writeIfWritten == 0)
            {
                return 0;
            }
            amountWritten += writeIfWritten;
            buffer += writeIfWritten;
        }

        const TwinStudio_VariantType valueType = structField->valueType & (~TwinStudio_VariantArray);
        const bool isArray = structField->valueType & TwinStudio_VariantArray;

        char* indexWriting = "";
        if (isArray)
        {
            int arrDecl = WriteFile(genFile, buffer, "   cJSON* "TS_VIEW_FORMAT"Json = cJSON_AddArrayToObject(rootObject, \""TS_VIEW_FORMAT"\");\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
            if (arrDecl == 0)
            {
                return 0;
            }
            amountWritten += arrDecl;
            buffer += arrDecl;

            int arrForLoopStart = WriteFile(genFile, buffer, "   for(uint32_t i = 0; i < arrlen(source->"TS_VIEW_FORMAT"); ++i) {\n", TS_VIEW_ARG(structField->name));
            if (arrForLoopStart == 0)
            {
                return 0;
            }
            amountWritten += arrForLoopStart;
            buffer += arrForLoopStart;

            if (valueType != TwinStudio_VariantString && valueType != TwinStudio_VariantObject)
            {
                int loopBodyStart = WriteFile(genFile, buffer, "      cJSON_AddItemToArray("TS_VIEW_FORMAT"Json, ", TS_VIEW_ARG(structField->name));
                if (loopBodyStart == 0)
                {
                    return 0;
                }
                amountWritten += loopBodyStart;
                buffer += loopBodyStart;
            }
            indexWriting = "[i]";
        }

        int fieldBodyWritten = 0;
        switch (valueType)
        {
            case TwinStudio_VariantBool:
                fieldBodyWritten = WriteFile(genFile, buffer, "   cJSON_AddBoolToObject(rootObject, \""TS_VIEW_FORMAT"\", source->"TS_VIEW_FORMAT"%s)", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name), indexWriting);
                break;
            case TwinStudio_VariantFloat:
            case TwinStudio_VariantInt8:
            case TwinStudio_VariantUInt8:
            case TwinStudio_VariantInt16:
            case TwinStudio_VariantUInt16:
            case TwinStudio_VariantInt32:
            case TwinStudio_VariantUInt32:
            case TwinStudio_VariantInt64:
            case TwinStudio_VariantUInt64:
            case TwinStudio_VariantEnum:
                fieldBodyWritten = WriteFile(genFile, buffer, "   cJSON_AddNumberToObject(rootObject, \""TS_VIEW_FORMAT"\", source->"TS_VIEW_FORMAT"%s)", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name), indexWriting);
                break;
            case TwinStudio_VariantString:
                if (!isArray)
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   char* dynCStr = TwinStudio_GetCStringDynamic(&source->"TS_VIEW_FORMAT");\n   cJSON_AddStringToObject(rootObject, \""TS_VIEW_FORMAT"\", dynCStr);\n   TWIN_FREE(dynCStr)", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                }
                else 
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   char* dynCStr = TwinStudio_GetCStringDynamic(&source->"TS_VIEW_FORMAT"[i]);\n   cJSON_AddItemToArray("TS_VIEW_FORMAT"Json, cJSON_CreateString(dynCStr));\n   TWIN_FREE(dynCStr", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                }
                break;
            case TwinStudio_VariantObject:
                if (!isArray)
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   cJSON_AddItemToObject(rootObject, \""TS_VIEW_FORMAT"\", "TS_VIEW_FORMAT"JsonSerialize(&source->"TS_VIEW_FORMAT"))", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name));
                }
                else
                {
                    fieldBodyWritten = WriteFile(genFile, buffer, "   cJSON_AddItemToArray("TS_VIEW_FORMAT"Json, "TS_VIEW_FORMAT"JsonSerialize(&source->"TS_VIEW_FORMAT"[i])", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name));
                }
                break;
            default:
                break;
        }
        if (fieldBodyWritten == 0)
        {
            return 0;
        }
        amountWritten += fieldBodyWritten;
        buffer += fieldBodyWritten;

        int fieldBodyEnd = WriteFile(genFile, buffer, isArray ? ");\n" : ";\n");
        if (fieldBodyEnd == 0)
        {
            return 0;
        }
        amountWritten += fieldBodyEnd;
        buffer += fieldBodyEnd;

        if (isArray)
        {
            int arrEndWritten = WriteFile(genFile, buffer, "   }\n");
            if (arrEndWritten == 0)
            {
                return 0;
            }
            amountWritten += arrEndWritten;
            buffer += arrEndWritten;
        }

        if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
        {
            int writeIfWritten = WriteFile(genFile, buffer, "   }\n");
            if (writeIfWritten == 0)
            {
                return 0;
            }
            amountWritten += writeIfWritten;
            buffer += writeIfWritten;
        }
    }

    return amountWritten;
}


static int GenerateStructFieldsJsonDeserialization(const TwinRes_ParserStructDefinition* structDef, TwinRes_GeneratedFile* genFile, char* buffer)
{
    int amountWritten = 0;
    int fieldsAmount = structDef->fields->length;
    if (structDef->isUnion && fieldsAmount > 1)
    {
        fieldsAmount = 1;
    }

    for (uint32_t i = 0; i < fieldsAmount; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            TwinRes_ParserStructDefinition* structDef = node->data;
            int innerStructWritten = GenerateStructFieldsJsonDeserialization(structDef, genFile, buffer);
            amountWritten += innerStructWritten;
            buffer += innerStructWritten;
            continue;
        }

        TwinRes_ParserStructFieldDefinition* structField = node->data;
        if (TS_VARIANT_GET(bool, structField->options.excludeFromJson.data))
        {
            continue;
        }

        const TwinStudio_VariantType valueType = structField->valueType & (~TwinStudio_VariantArray);
        const bool isArray = structField->valueType & TwinStudio_VariantArray;

        char constructorBuffer[1024];
        constructorBuffer[0] = '\0';
        if (TS_VARIANT_GET(bool, structField->options.constructor.data) && structField->options.constructor.argumentLength > 0)
        {
            for (uint32_t j = 0; j < structField->options.constructor.argumentLength; ++j)
            {
                if (j > 0 && j < structField->options.constructor.argumentLength - 1)
                {
                    strcat(constructorBuffer, ", ");
                }

                char paramBuffer[128];
                snprintf(paramBuffer, 128, "target->"TS_VIEW_FORMAT, TS_VIEW_ARG(structField->options.constructor.arguments[j].storage.string));
                strcat(constructorBuffer, paramBuffer);
            }
        }

        if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
        {
            TwinStudio_StringView originalCondition = TS_VARIANT_GET(TwinStudio_StringView, structField->options.writeIf.arguments[0]);
            TwinStudio_StringView conditionCopy = TwinStudio_CopyFromCString(TwinStudio_GetCStringDynamic(&originalCondition));
            char* replacement = NULL;
            char* currentSearch = conditionCopy.dynString;
            while ((replacement = strstr(currentSearch, "source")) != NULL)
            {
                memcpy(replacement, "target", sizeof("target") - 1);
                currentSearch = replacement;
            }
            int writeIfWritten = WriteFile(genFile, buffer, "   if ("TS_VIEW_FORMAT") {\n", TS_VIEW_ARG(conditionCopy));
            TwinStudio_FreeString(&conditionCopy);
            if (writeIfWritten == 0)
            {
                return 0;
            }
            amountWritten += writeIfWritten;
            buffer += writeIfWritten;
        }

        if (isArray)
        {
            int arrLenWritten = WriteFile(genFile, buffer, "   cJSON* "TS_VIEW_FORMAT"Json = cJSON_GetObjectItemCaseSensitive(json, \""TS_VIEW_FORMAT"\");\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
            if (arrLenWritten == 0)
            {
                return 0;
            }
            amountWritten += arrLenWritten;
            buffer += arrLenWritten;

            int arrForLoopStart = WriteFile(genFile, buffer, "   { cJSON* jsonItem; cJSON_ArrayForEach(jsonItem, "TS_VIEW_FORMAT"Json) {\n", TS_VIEW_ARG(structField->name));
            if (arrForLoopStart == 0)
            {
                return 0;
            }
            amountWritten += arrForLoopStart;
            buffer += arrForLoopStart;

            if (valueType != TwinStudio_VariantObject)
            {
                int prePutWritten = WriteFile(genFile, buffer, "      arrput(target->"TS_VIEW_FORMAT", ", TS_VIEW_ARG(structField->name));
                if (prePutWritten == 0)
                {
                    return 0;
                }
                amountWritten += prePutWritten;
                buffer += prePutWritten;
            }

            int fieldBodyWritten = 0;
            switch (valueType)
            {
                case TwinStudio_VariantBool:
                    fieldBodyWritten = WriteFile(genFile, buffer, "jsonItem->valueint);\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    break;
                case TwinStudio_VariantFloat:
                case TwinStudio_VariantInt8:
                case TwinStudio_VariantUInt8:
                case TwinStudio_VariantInt16:
                case TwinStudio_VariantUInt16:
                case TwinStudio_VariantInt32:
                case TwinStudio_VariantUInt32:
                case TwinStudio_VariantInt64:
                case TwinStudio_VariantUInt64:
                case TwinStudio_VariantEnum:
                    fieldBodyWritten = WriteFile(genFile, buffer, "jsonItem->valuedouble);\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    break;
                case TwinStudio_VariantString:
                    fieldBodyWritten = WriteFile(genFile, buffer, "TwinStudio_CopyFromCString(jsonItem->valuestring));\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    break;
                case TwinStudio_VariantObject:
                    fieldBodyWritten = WriteFile(genFile, buffer, "      "TS_VIEW_FORMAT" createdObj = "TS_VIEW_FORMAT"Create(%s); "TS_VIEW_FORMAT"JsonDeserialize(&createdObj, jsonItem); arrput(target->"TS_VIEW_FORMAT", createdObj);\n", TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->type), constructorBuffer, TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name));
                    break;
                default:
                    break;
            }
            if (fieldBodyWritten == 0)
            {
                return 0;
            }
            amountWritten += fieldBodyWritten;
            buffer += fieldBodyWritten;
        }
        else
        {
            int fieldBodyWritten = 0;
            switch (valueType)
            {
                case TwinStudio_VariantBool:
                    fieldBodyWritten = WriteFile(genFile, buffer, "   target->"TS_VIEW_FORMAT" = cJSON_GetObjectItemCaseSensitive(json, \""TS_VIEW_FORMAT"\")->valueint;\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    break;
                case TwinStudio_VariantFloat:
                case TwinStudio_VariantInt8:
                case TwinStudio_VariantUInt8:
                case TwinStudio_VariantInt16:
                case TwinStudio_VariantUInt16:
                case TwinStudio_VariantInt32:
                case TwinStudio_VariantUInt32:
                case TwinStudio_VariantInt64:
                case TwinStudio_VariantUInt64:
                case TwinStudio_VariantEnum:
                    fieldBodyWritten = WriteFile(genFile, buffer, "   target->"TS_VIEW_FORMAT" = cJSON_GetObjectItemCaseSensitive(json, \""TS_VIEW_FORMAT"\")->valuedouble;\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    break;
                case TwinStudio_VariantString:
                    fieldBodyWritten = WriteFile(genFile, buffer, "   target->"TS_VIEW_FORMAT" = TwinStudio_CopyFromCString(cJSON_GetObjectItemCaseSensitive(json, \""TS_VIEW_FORMAT"\")->valuestring);\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    break;
                case TwinStudio_VariantObject:
                    fieldBodyWritten = WriteFile(genFile, buffer, "   "TS_VIEW_FORMAT"JsonDeserialize(&target->"TS_VIEW_FORMAT", cJSON_GetObjectItemCaseSensitive(json, \""TS_VIEW_FORMAT"\"));\n", TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
                    break;
                default:
                    break;
            }
            if (fieldBodyWritten == 0)
            {
                return 0;
            }
            amountWritten += fieldBodyWritten;
            buffer += fieldBodyWritten;
        }

        if (isArray)
        {
            int arrEndWritten = WriteFile(genFile, buffer, "   }}\n");
            if (arrEndWritten == 0)
            {
                return 0;
            }
            amountWritten += arrEndWritten;
            buffer += arrEndWritten;
        }

        if (TS_VARIANT_GET(bool, structField->options.writeIf.data))
        {
            int writeIfWritten = WriteFile(genFile, buffer, "   }\n");
            if (writeIfWritten == 0)
            {
                return 0;
            }
            amountWritten += writeIfWritten;
            buffer += writeIfWritten;
        }
    }

    return amountWritten;
}


static int GenerateStructDefinitions(TwinRes_ParserNode* node, TwinRes_GeneratedFile* genFile, char* buffer)
{
    assert(node->type == TwinRes_NodeStructDefinition);

    static const char structStartFormat[] = "struct "TS_VIEW_FORMAT" {\n";

    const TwinRes_ParserStructDefinition* structDef = node->data;
    const TwinStudio_StringView structName = structDef->structName;

    int amountWritten = 0;
    static const char constructorDecl[] = TS_VIEW_FORMAT" "TS_VIEW_FORMAT"Create(%s)\n{\n   "TS_VIEW_FORMAT" result = { \n";
    char constructorParamsBuffer[2048];
    constructorParamsBuffer[0] = '\0';
    bool hasParams = false;
    for (uint32_t i = 0; i < structDef->fields->length; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            continue;
        }

        TwinRes_ParserStructFieldDefinition* structField = node->data;
        if (!TS_VARIANT_GET(bool, structField->options.constructor.data) || structField->options.constructor.argumentLength > 0)
        {
            continue;
        }

        char paramBuffer[128];
        snprintf(paramBuffer, 128, TS_VIEW_FORMAT" "TS_VIEW_FORMAT"Param", TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name));

        structField->valueFromConstructor = true;
        if (!hasParams)
        {
            hasParams = true;
        }
        else
        {
            strcat(constructorParamsBuffer, ", ");
        }
        strcat(constructorParamsBuffer, paramBuffer);
    }

    int constructorWritten = WriteFile(genFile, buffer, constructorDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName), constructorParamsBuffer, TS_VIEW_ARG(structDef->structName));
    if (constructorWritten == 0)
    {
        return 0;
    }
    amountWritten += constructorWritten;
    buffer += constructorWritten;

    for (uint32_t i = 0; i < structDef->fields->length; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            continue;
        }

        TwinRes_ParserStructFieldDefinition* structField = node->data;

        if (structField->valueFromConstructor)
        {
            int constructorWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = "TS_VIEW_FORMAT"Param,\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->name));
            if (constructorWritten == 0)
            {
                return 0;
            }
            amountWritten += constructorWritten;
            buffer += constructorWritten;
            continue;
        }

        if (structField->defaultValue.type == TwinStudio_VariantNull)
        {
            continue;
        }

        int fieldBodyWritten = 0;
        const TwinStudio_VariantType defaultType = structField->defaultValue.type & (~TwinStudio_VariantArray);
        switch (defaultType)
        {
            case TwinStudio_VariantBool:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.isEnabled);
                break;
            case TwinStudio_VariantFloat:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %ff,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.floating);
                break;
            case TwinStudio_VariantEnum:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = "TS_VIEW_FORMAT",\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->defaultValue.storage.string));
                break;
            case TwinStudio_VariantInt8:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.integer8);
                break;
            case TwinStudio_VariantUInt8:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.uinteger8);
                break;
            case TwinStudio_VariantInt16:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.integer16);
                break;
            case TwinStudio_VariantUInt16:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.uinteger16);
                break;
            case TwinStudio_VariantInt32:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.integer);
                break;
            case TwinStudio_VariantUInt32:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.uinteger);
                break;
            case TwinStudio_VariantInt64:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.integer64);
                break;
            case TwinStudio_VariantUInt64:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = %d,\n", TS_VIEW_ARG(structField->name), structField->defaultValue.storage.uinteger64);
                break;
            case TwinStudio_VariantString:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = \""TS_VIEW_FORMAT"\",\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->defaultValue.storage.string));
                break;
            case TwinStudio_VariantObject:
                fieldBodyWritten = WriteFile(genFile, buffer, "      ."TS_VIEW_FORMAT" = ("TS_VIEW_FORMAT") "TS_VIEW_FORMAT",\n", TS_VIEW_ARG(structField->name), TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->defaultValue.storage.string));
                break;
            default:
                break;
        }
        if (fieldBodyWritten == 0)
        {
            return 0;
        }
        amountWritten += fieldBodyWritten;
        buffer += fieldBodyWritten;
    }

    static const char constructorEndDecl[] = "   };\n   return result;\n}\n\n";
    int constructorEndWritten = WriteFile(genFile, buffer, constructorEndDecl);
    if (constructorEndWritten == 0)
    {
        return 0;
    }
    amountWritten += constructorEndWritten;
    buffer += constructorEndWritten;


    // Generate serialization definitions
    const bool excludeJsonSerial = TS_VARIANT_GET(bool, structDef->options.excludeFromJson.data);
    const bool excludeBinSerial  = TS_VARIANT_GET(bool, structDef->options.excludeFromBin.data);
    const bool excludeDeserial   = TS_VARIANT_GET(bool, structDef->options.noRead.data);
    const bool excludeSerial     = TS_VARIANT_GET(bool, structDef->options.noWrite.data);

    if (!excludeBinSerial)
    {
        static const char binSerialDecl[] = "void "TS_VIEW_FORMAT"BinSerialize("TS_VIEW_FORMAT"* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)\n{\n";
        static const char binDeserialDecl[] = "void "TS_VIEW_FORMAT"BinDeserialize("TS_VIEW_FORMAT"* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)\n{\n";

        if (!excludeSerial)
        {
            int declWritten = WriteFile(genFile, buffer, binSerialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;

            int fieldWritten = GenerateStructFieldsBinarySerialization(structDef, genFile, buffer);
            amountWritten += fieldWritten;
            buffer += fieldWritten;

            static const char binSerialBody[] = "}\n\n";
            int bodyWritten = WriteFile(genFile, buffer, binSerialBody, TS_VIEW_ARG(structName));
            if (bodyWritten == 0)
            {
                return 0;
            }
            amountWritten += bodyWritten;
            buffer += bodyWritten;
        }

        if (!excludeDeserial)
        {
            int declWritten = WriteFile(genFile, buffer, binDeserialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;

            int fieldsWritten = GenerateStructFieldsBinaryDeserialization(structDef, genFile, buffer);
            amountWritten += fieldsWritten;
            buffer += fieldsWritten;

            static const char binDeserialBody[] = "}\n\n";
            int bodyWritten = WriteFile(genFile, buffer, binDeserialBody, TS_VIEW_ARG(structName));
            if (bodyWritten == 0)
            {
                return 0;
            }
            amountWritten += bodyWritten;
            buffer += bodyWritten;
        }
    }

    if (!excludeJsonSerial)
    {
        static const char jsonSerialDecl[] = "cJSON* "TS_VIEW_FORMAT"JsonSerialize("TS_VIEW_FORMAT"* source)\n{\n";
        static const char jsonDeserialDecl[] = "void "TS_VIEW_FORMAT"JsonDeserialize("TS_VIEW_FORMAT"* target, cJSON* json)\n{\n";
        
        
        if (!excludeSerial)
        {
            int declWritten = WriteFile(genFile, buffer, jsonSerialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;

            static const char jsonSerialBody[] = "   cJSON* rootObject = cJSON_CreateObject();\n";
            int bodyWritten = WriteFile(genFile, buffer, jsonSerialBody);
            if (bodyWritten == 0)
            {
                return 0;
            }
            amountWritten += bodyWritten;
            buffer += bodyWritten;

            int fieldsWritten = GenerateStructFieldsJsonSerialization(structDef, genFile, buffer);
            amountWritten += fieldsWritten;
            buffer += fieldsWritten;

            int bodyEndWritten = WriteFile(genFile, buffer, "   return rootObject;\n}\n\n");
            if (bodyEndWritten == 0)
            {
                return 0;
            }
            amountWritten += bodyEndWritten;
            buffer += bodyEndWritten;
        }

        if (!excludeDeserial)
        {
            int declWritten = WriteFile(genFile, buffer, jsonDeserialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;

            int fieldsWritten = GenerateStructFieldsJsonDeserialization(structDef, genFile, buffer);
            amountWritten += fieldsWritten;
            buffer += fieldsWritten;

            int bodyEndWritten = WriteFile(genFile, buffer, "   cJSON_Delete(json);\n}\n\n");
            if (bodyEndWritten == 0)
            {
                return 0;
            }
            amountWritten += bodyEndWritten;
            buffer += bodyEndWritten;
        }
    }


    return amountWritten;
}


static int GenerateStructDeclarations(TwinRes_ParserNode* node, TwinRes_GeneratedFile* genFile, char* buffer, bool isInlineStruct)
{
    assert(node->type == TwinRes_NodeStructDefinition);

    static const char structStartFormat[] = "\n\n\ntypedef struct "TS_VIEW_FORMAT" {\n";
    static const char inlineStructStartFormat[] = "\n\n\nTS_COMPACT_STRUCT {\n";
    static const char unionStartFormat[] = "\n\n\nTS_COMPACT_UNION {\n";
    static const char structEndFormat[] = "} "TS_VIEW_FORMAT";\n";
    static const char inlineStructEndFormat[] = "};\n";

    // Write struct header definition
    const TwinRes_ParserStructDefinition* structDef = node->data;
    const TwinStudio_StringView structName = structDef->structName;
    int amountWritten = 0;

    char* startFormat = (void*)structStartFormat;
    char* endFormat   = (void*)structEndFormat;
    if (isInlineStruct)
    {
        startFormat = (void*)inlineStructStartFormat;
        endFormat = (void*)inlineStructEndFormat;
    }
    if (structDef->isUnion)
    {
        startFormat = (void*)unionStartFormat;
        endFormat = (void*)inlineStructEndFormat;
    }

    int structHeaderWritten = WriteFile(genFile, buffer, startFormat, TS_VIEW_ARG(structName));
    if (structHeaderWritten == 0)
    {
        return 0;
    }
    amountWritten += structHeaderWritten;
    buffer += structHeaderWritten;

    // Write fields
    for (uint32_t i = 0; i < structDef->fields->length; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            int inlineStructWritten = GenerateStructDeclarations(node, genFile, buffer, true);
            if (inlineStructWritten == 0)
            {
                return 0;
            }
            amountWritten += inlineStructWritten;
            buffer += inlineStructWritten;
            continue;
        }

        int fieldAmountWritten = GenerateStructField(node, genFile, buffer);
        if (fieldAmountWritten == 0)
        {
            return 0;
        }

        amountWritten += fieldAmountWritten;
        buffer += fieldAmountWritten;
    }

    int endWritten = WriteFile(genFile, buffer, endFormat, TS_VIEW_ARG(structName));
    if (endWritten == 0)
    {
        return 0;
    }

    amountWritten += endWritten;
    buffer += endWritten;

    if (isInlineStruct || structDef->isUnion)
    {
        return amountWritten;
    }

    static const char constructorDecl[] = TS_VIEW_FORMAT" "TS_VIEW_FORMAT"Create(%s);\n";
    char constructorParamsBuffer[2048];
    constructorParamsBuffer[0] = '\0';
    bool hasParams = false;
    for (uint32_t i = 0; i < structDef->fields->length; ++i)
    {
        TwinRes_ParserNode* node = structDef->fields->nodes + i;
        if (node->type == TwinRes_NodeStructDefinition)
        {
            continue;
        }

        TwinRes_ParserStructFieldDefinition* structField = node->data;
        if (!TS_VARIANT_GET(bool, structField->options.constructor.data) || structField->options.constructor.argumentLength > 0)
        {
            continue;
        }

        char paramBuffer[128];
        snprintf(paramBuffer, 128, TS_VIEW_FORMAT" "TS_VIEW_FORMAT"Param", TS_VIEW_ARG(structField->type), TS_VIEW_ARG(structField->name));

        structField->valueFromConstructor = true;
        if (!hasParams)
        {
            hasParams = true;
        }
        else
        {
            strcat(constructorParamsBuffer, ", ");
        }
        strcat(constructorParamsBuffer, paramBuffer);
    }

    int constructorWritten = WriteFile(genFile, buffer, constructorDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName), constructorParamsBuffer);
    if (constructorWritten == 0)
    {
        return 0;
    }
    amountWritten += constructorWritten;
    buffer += constructorWritten;

    // Generate serialization declaration
    const bool excludeJsonSerial = TS_VARIANT_GET(bool, structDef->options.excludeFromJson.data);
    const bool excludeBinSerial  = TS_VARIANT_GET(bool, structDef->options.excludeFromBin.data);
    const bool excludeDeserial   = TS_VARIANT_GET(bool, structDef->options.noRead.data);
    const bool excludeSerial     = TS_VARIANT_GET(bool, structDef->options.noWrite.data);
    if (!excludeBinSerial)
    {
        static const char binSerialDecl[] = "void "TS_VIEW_FORMAT"BinSerialize("TS_VIEW_FORMAT"* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);\n";
        static const char binDeserialDecl[] = "void "TS_VIEW_FORMAT"BinDeserialize("TS_VIEW_FORMAT"* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);\n";
        
        if (!excludeSerial)
        {
            int declWritten = WriteFile(genFile, buffer, binSerialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;
        }

        if (!excludeDeserial)
        {
            int declWritten = WriteFile(genFile, buffer, binDeserialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;
        }
    }

    if (!excludeJsonSerial)
    {
        static const char jsonSerialDecl[] = "cJSON* "TS_VIEW_FORMAT"JsonSerialize("TS_VIEW_FORMAT"* source);\n";
        static const char jsonDeserialDecl[] = "void "TS_VIEW_FORMAT"JsonDeserialize("TS_VIEW_FORMAT"* target, cJSON* json);\n";
        
        if (!excludeSerial)
        {
            int declWritten = WriteFile(genFile, buffer, jsonSerialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;
        }

        if (!excludeDeserial)
        {
            int declWritten = WriteFile(genFile, buffer, jsonDeserialDecl, TS_VIEW_ARG(structDef->structName), TS_VIEW_ARG(structDef->structName));
            if (declWritten == 0)
            {
                return 0;
            }
            amountWritten += declWritten;
            buffer += declWritten;
        }
    }

    return amountWritten;
}


static int GenerateEnumDefinition(TwinRes_ParserNode* node, TwinRes_GeneratedFile* genFile, char* buffer)
{
    assert(node->type == TwinRes_NodeEnumDefinition);

    static const char enumStartFormat[] = "typedef enum {\n";
    static const char enumEndFormat[] = "} "TS_VIEW_FORMAT";\n\n\n";

    TwinRes_ParserEnumDefinition* enumDef = node->data;

    int amountWritten = WriteFile(genFile, buffer, enumStartFormat);
    if (amountWritten == 0)
    {
        return 0;
    }
    buffer += amountWritten;

    static const char enumNameFormat[] = "   "TS_VIEW_FORMAT",\n";
    static const char enumNameCustomFormat[] = "   "TS_VIEW_FORMAT" = %d,\n";
    for (uint32_t i = 0; i < enumDef->enums->length; ++i)
    {
        TwinRes_ParserEnumName* enumNameDef = enumDef->enums->nodes[i].data;
        int nameAmountWritten = 0;
        if (enumNameDef->isCustomValue)
        {
            nameAmountWritten += WriteFile(genFile, buffer, enumNameCustomFormat, TS_VIEW_ARG(enumNameDef->name), enumNameDef->value);
        }
        else
        {
            nameAmountWritten += WriteFile(genFile, buffer, enumNameFormat, TS_VIEW_ARG(enumNameDef->name));
        }
        if (nameAmountWritten == 0)
        {
            return 0;
        }

        amountWritten += nameAmountWritten;
        buffer += nameAmountWritten;
    }

    int endWritten = WriteFile(genFile, buffer, enumEndFormat, TS_VIEW_ARG(enumDef->enumName));
    if (endWritten == 0)
    {
        return 0;
    }

    return amountWritten + endWritten;
}


static void TwinRes_ParseAndGenerateStructFiles(TwinRes_GeneratorOutput* output, const char* name)
{
    char* headerSourceStr = output->structFiles[TwinRes_GenHeader].data;
    char* implSourceStr = output->structFiles[TwinRes_GenImplementation].data;
    headerSourceStr += WriteFile(&output->structFiles[TwinRes_GenHeader], headerSourceStr, autoGenFileStartTemplate);
    implSourceStr += WriteFile(&output->structFiles[TwinRes_GenImplementation], implSourceStr, autoGenFileStartTemplate);

    headerSourceStr += WriteFile(&output->structFiles[TwinRes_GenHeader], headerSourceStr, structHeaderFileStartTemplate, name, name);

    static const char defaultIncludes[] = "#include <cJSON.h>\n"
        "#include <stdint.h>\n"
        "#include <stb_ds.h>\n"
        "#include \"defines/defines.h\"\n"
        "#include \"memory/memory.h\"\n"
        "#include \"resources/resources.h\"\n"
        "#include \"serialization/binary_serializer.h\"\n"
        "#include \"serialization/helpers.h\"\n"
        "#include \"string_view/string_view.h\"\n";
    headerSourceStr += WriteFile(&output->structFiles[TwinRes_GenHeader], headerSourceStr, defaultIncludes);

    implSourceStr += WriteFile(&output->structFiles[TwinRes_GenImplementation], implSourceStr, defaultIncludes);
    implSourceStr += WriteFile(&output->structFiles[TwinRes_GenImplementation], implSourceStr, structImplementationFileStartTemplate, output->structFiles[TwinRes_GenHeader].fileName);

    TwinRes_ParserNodeList* nodeList = output->ast->data;
    for (uint32_t i = 0; i < nodeList->length; ++i)
    {
        TwinRes_ParserNode node = nodeList->nodes[i];
        switch (node.type) {
            case TwinRes_NodeInclude:
                headerSourceStr += GenerateInclude(&node, &output->structFiles[TwinRes_GenHeader], headerSourceStr);
                implSourceStr += GenerateInclude(&node, &output->structFiles[TwinRes_GenImplementation], implSourceStr);
                break;
            case TwinRes_NodeStructDefinition:
                headerSourceStr += GenerateStructDeclarations(&node, &output->structFiles[TwinRes_GenHeader], headerSourceStr, false);
                implSourceStr   += GenerateStructDefinitions(&node, &output->structFiles[TwinRes_GenImplementation], implSourceStr);
                break;
            case TwinRes_NodeEnumDefinition:
                headerSourceStr += GenerateEnumDefinition(&node, &output->structFiles[TwinRes_GenHeader], headerSourceStr);
                break;
            default:
                break;
        }
    }

    WriteFile(&output->structFiles[TwinRes_GenHeader], headerSourceStr, structHeaderFileEndTemplate, name);
}


static void TwinRes_ParseAndGenerateUiFiles(TwinRes_GeneratorOutput* output, const char* name)
{
    
}


TwinRes_GeneratorOutput TwinRes_ParseAndGenerate(const char* name, char* string)
{
    TwinRes_GeneratorOutput output = CreateOutput(name);
    TwinRes_Lexer lexer = TwinRes_LexerCreate(string);

    bool isTokInvalid = false;
    while (lexer.curTok.tokenType != TwinRes_TokEof)
    {
        TwinRes_LexerToken tok = lexer.curTok;
        
        switch (tok.tokenType) {
            case TwinRes_TokHash: // Includes
                AppendParserNode(output.ast->data, IncludeNode(&output, &lexer));
                break;
            case TwinRes_TokAttribOpen:
            case TwinRes_TokIdentifier: // asset definitions
                if (IsIdentifier(tok.string, TS_STRING_VIEW(assetIdentifier)) || tok.tokenType == TwinRes_TokAttribOpen)
                {
                    AppendParserNode(output.ast->data, StructDefinitionNode(&output, &lexer));
                }
                else if (IsIdentifier(tok.string, TS_STRING_VIEW(enumIdentifier)))
                {
                    AppendParserNode(output.ast->data, EnumDefinitionNode(&output, &lexer));
                }
                else
                {
                    isTokInvalid = true;
                }
                break;
            default:
                isTokInvalid = true;
                break;
        }

        if (output.status == TwinRes_GeneratorFail)
        {
            break;
        }

        if (isTokInvalid)
        {
            if (tok.tokenType == TwinRes_TokIdentifier)
            {
                snprintf(output.errorString, maxErrorStringLength, "Invalid root token "TS_VIEW_FORMAT, TS_VIEW_ARG(tok.string));
            }
            else
            {
                snprintf(output.errorString, maxErrorStringLength, "Invalid root token %d", tok.tokenType);
            }
            output.status = TwinRes_GeneratorFail;
            break;
        }
    }

    // After creating the AST generate the files

    if (output.status == TwinRes_GeneratorSuccess)
    {
        TwinRes_ParseAndGenerateStructFiles(&output, name);
        TwinRes_ParseAndGenerateUiFiles(&output, name);
    }

    return output;
}


void TwinRes_FreeGeneratedOutput(TwinRes_GeneratorOutput* output)
{
    TwinStudio_ArenaFree(&output->generatorArena);
}