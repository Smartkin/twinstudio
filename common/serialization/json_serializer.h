#ifndef TS_JSON_SERIALIZER_H
#define TS_JSON_SERIALIZER_H

#include "cJSON.h"
#include "string_view/string_view.h"
#include <stddef.h>

void TwinStudio_JsonSerializerInit();
void TwinStudio_JsonWriteToFile(TwinStudio_StringView path, const cJSON* json);
cJSON* TwinStudio_JsonReadFromFile(TwinStudio_StringView path);

#endif // TS_JSON_SERIALIZER_H