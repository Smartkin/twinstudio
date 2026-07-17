#include "agent_lab_helper.h"
#include "ps2/retail/rm2/code/auto_struct_agent_lab.h"
#include <stdint.h>


void TwinRes_BehaviorGraphBinSerializeFull(TwinRes_BehaviorGraph* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_BehaviorGraphBinSerialize(source, serializer, arena, size, userData);

    for (uint32_t i = 0; i < arrlen(source->states); ++i)
    {
        for (uint32_t j = 0; j < source->states[i].bodiesAmount; ++j)
        {
            TwinRes_BehaviorStateBodyBinSerialize(source->states[i].bodies + j, serializer, arena, size, source->states + i);
        }
    }
}

void TwinRes_BehaviorGraphBinDeserializeFull(TwinRes_BehaviorGraph* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_BehaviorGraphBinDeserialize(target, deserializer, arena, size, userData);

    for (uint32_t i = 0; i < arrlen(target->states); ++i)
    {
        arrsetcap(target->states[i].bodies, target->states[i].bodiesAmount);
        for (uint32_t j = 0; j < target->states[i].bodiesAmount; ++j)
        {
            TwinRes_BehaviorStateBody createdObj;
            TwinRes_BehaviorStateBodyBinDeserialize(target->states[i].bodies + j, deserializer, arena, size, target->states + i);
            arrput(target->states[i].bodies, createdObj);
        }
    }
}
