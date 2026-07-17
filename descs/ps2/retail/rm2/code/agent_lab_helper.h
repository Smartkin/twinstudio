#ifndef TR_AGENT_LAB_HELPER_H
#define TR_AGENT_LAB_HELPER_H

#include "auto_struct_agent_lab.h"


// These are cursed hacks because state bodies are serialized in a not good way :(
void TwinRes_BehaviorGraphBinSerializeFull(TwinRes_BehaviorGraph* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_BehaviorGraphBinDeserializeFull(TwinRes_BehaviorGraph* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);


#endif // TR_AGENT_LAB_HELPER_H