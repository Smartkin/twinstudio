#ifndef TR_PARSER_H
#define TR_PARSER_H


#include "memory/memory.h"
#include "string_view/string_view.h"
#include "variant/variant.h"

#include <stdint.h>

struct TwinRes_ParserNode;

typedef enum {
    TwinRes_NodeEmpty,
    TwinRes_NodeEnumDefinition,
    TwinRes_NodeEnumName,
    TwinRes_NodeStructDefinition,
    TwinRes_NodeFieldDefinition,
    TwinRes_NodeInclude,
    TwinRes_NodeAttribute,
    TwinRes_NodeAssignment,
    TwinRes_NodeList,
    TwinRes_NodeLiteral,

    TwinRes_NodeCount,
} TwinRes_NodeTypes;


typedef struct TwinRes_AttributeArgumentsList {
    uint32_t length;
    uint32_t capacity;
    TwinStudio_Variant arguments[];
} TwinRes_AttributeArgumentsList;


typedef struct TwinRes_AttributeInfo {
    TwinStudio_StringView name;
    TwinStudio_Variant data;
    TwinStudio_Variant* arguments;
    uint32_t argumentLength;
} TwinRes_AttributeInfo;


typedef struct TwinRes_StructGeneratorOptions {
    TwinRes_AttributeInfo useCustomSerialization;
    TwinRes_AttributeInfo useViewport;
    TwinRes_AttributeInfo caption;
    TwinRes_AttributeInfo excludeFromBin;
    TwinRes_AttributeInfo excludeFromJson;
    TwinRes_AttributeInfo noRead;
    TwinRes_AttributeInfo noWrite;
    TwinRes_AttributeInfo writeAll;
} TwinRes_StructGeneratorOptions;


typedef struct TwinRes_StructFieldGeneratorOptions {
    TwinRes_AttributeInfo caption;
    TwinRes_AttributeInfo visibleIf;
    TwinRes_AttributeInfo writeIf;
    TwinRes_AttributeInfo noLength;
    TwinRes_AttributeInfo length;
    TwinRes_AttributeInfo align;
    TwinRes_AttributeInfo isEnum;
    TwinRes_AttributeInfo excludeFromBin;
    TwinRes_AttributeInfo excludeFromJson;
    TwinRes_AttributeInfo resource;
    TwinRes_AttributeInfo blob;
    TwinRes_AttributeInfo toBigEndian;
    TwinRes_AttributeInfo linkResource;
    TwinRes_AttributeInfo voidRead;
    TwinRes_AttributeInfo constructor;
} TwinRes_StructFieldGeneratorOptions;


typedef struct TwinRes_ParserNode {
    union {
        void* data;
        int32_t integer;
        float floating;
        TwinStudio_StringView string;
    };

    TwinRes_NodeTypes type;
} TwinRes_ParserNode;


typedef struct TwinRes_ParserNodeList {
    uint32_t length;
    uint32_t capacity;
    TwinRes_ParserNode nodes[];
} TwinRes_ParserNodeList;


typedef struct TwinRes_ParserStructFieldDefinition {
    TwinStudio_StringView type;
    TwinStudio_StringView name;
    TwinRes_StructFieldGeneratorOptions options;
    TwinStudio_Variant defaultValue;
    TwinStudio_VariantType valueType;
    int32_t bitfield;
    bool isConst;
    bool valueFromConstructor;
} TwinRes_ParserStructFieldDefinition;


typedef struct TwinRes_ParserStructDefinition {
    TwinRes_ParserNodeList* fields;
    TwinRes_StructGeneratorOptions options;
    TwinStudio_StringView structName;
    bool isUnion;
} TwinRes_ParserStructDefinition;


typedef struct TwinRes_ParserEnumName {
    int32_t value;
    TwinStudio_StringView name;
    bool isCustomValue;
} TwinRes_ParserEnumName;


typedef struct TwinRes_ParserEnumDefinition {
    TwinRes_ParserNodeList* enums;
    TwinStudio_StringView enumName;
} TwinRes_ParserEnumDefinition;


typedef struct TwinRes_GeneratedFile {
    const char* fileName;
    char* data;
    int length;
} TwinRes_GeneratedFile;


#define TR_PARSER_FILE_FORMAT "%.*s"
#define TR_PARSER_FILE_ARG(str) (str).length, (str).data


typedef enum {
    TwinRes_GenHeader,
    TwinRes_GenImplementation,
    TwinRes_UserImplementation,

    TwinRes_MaxGenFiles
} TwinRes_GenFileType;


typedef enum {
    TwinRes_GeneratorSuccess,
    TwinRes_GeneratorFail
} TwinRes_GeneratorStatus;


typedef struct TwinRes_GeneratorOutput {
    TwinStudio_Arena generatorArena;

    TwinRes_GeneratedFile structFiles[TwinRes_MaxGenFiles];
    TwinRes_GeneratedFile uiFiles[TwinRes_MaxGenFiles];

    TwinRes_ParserNode* ast;

    char* errorString;
    TwinRes_GeneratorStatus status;
} TwinRes_GeneratorOutput;


TwinRes_GeneratorOutput TwinRes_ParseAndGenerate(const char* name, char* string);
void TwinRes_FreeGeneratedOutput(TwinRes_GeneratorOutput* output);

#endif // TR_PARSER_H