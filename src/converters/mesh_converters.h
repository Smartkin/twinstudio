#ifndef TS_MESH_CONVERTERS_H
#define TS_MESH_CONVERTERS_H

#include "memory/memory.h"
#include "ps2/retail/rm2/code/auto_struct_body.h"
#include "render/mesh.h"
#include "resources/chunk_resources.h"

TwinStudio_RenderBody TwinStudio_ConvertPs2Body(TwinStudio_ChunkResourceManager* chunkRes, TwinRes_Body* twinBody, TwinStudio_Arena* arena);

#endif // TS_MESH_CONVERTERS_H