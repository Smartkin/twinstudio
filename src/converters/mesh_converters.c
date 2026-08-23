#include "mesh_converters.h"
#include "ps2/dma_tag.h"
#include "ps2/retail/graphics/auto_struct_blend_skin.h"
#include "ps2/retail/graphics/auto_struct_material.h"
#include "ps2/retail/graphics/auto_struct_model.h"
#include "ps2/retail/graphics/auto_struct_rigid_model.h"
#include "ps2/retail/graphics/auto_struct_skin.h"
#include "ps2/retail/graphics/auto_struct_texture.h"
#include "ps2/retail/rm2/code/auto_struct_body.h"
#include "ps2/vif.h"
#include "raylib.h"
#include "resources/chunk_resources.h"
#include "resources/resources.h"
#include "memory/memory.h"
#include "render/mesh.h"
#include "serialization/binary_serializer.h"
#include <assert.h>
#include <stb_ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>


#define BLEND_SKIN_INDEX_MAP_SIZE 4096


static void BuildBlendSkinMesh(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_RenderBody* renderBody, TwinRes_BlendSkin* blendSkin, TwinStudio_Arena* arena)
{
    TwinStudio_Mesh mesh = { 0 };

    for (size_t i = 0; i < arrlen(blendSkin->models); ++i)
    {
        TwinRes_BlendSkinModel* blendModel = blendSkin->models + i;
        TwinRes_Material* material = TwinStudio_GetChunkResource(chunkRes, blendModel->materialLinkType, TS_SRT_None, blendModel->material).data;
        int32_t indexMap[BLEND_SKIN_INDEX_MAP_SIZE];
        uint32_t indexMapIdx = 0;
        for (size_t j = 0; j < arrlen(blendModel->modelParts); ++j)
        {
            TwinRes_BlendSkinModelPart* blendModelPart = blendModel->modelParts + j;
            TwinStudio_VIFOutput output = TwinStudio_VIFInterpretData(blendModelPart->vifData, blendModelPart->vifDataLength, arena);

            TwinStudio_Material studioMaterial = { 0 };
            studioMaterial.name = TwinStudio_CopyFromCString(material->name.string);
            for (size_t k = 0; k < arrlen(material->shaders); ++k)
            {
                TwinRes_MaterialShader* shader = material->shaders + k;
                TwinRes_Texture* texture = TwinStudio_GetChunkResource(chunkRes, shader->textureLinkType, TS_SRT_None, shader->texture).data;
                arrput(studioMaterial.textures, texture->textureData);
            }
            arrput(mesh.materials, studioMaterial);

            for (size_t k = 0; k < arrlen(output.vertexes); ++k)
            {
                TwinStudio_MeshVertex vertex = { 0 };
                TwinStudio_VIFVector* posVec = output.vertexes + k;
                TwinStudio_VIFVector* uvVec  = output.uvColors + k;
                TwinStudio_VIFVector* colorVec = output.emits + k;
                posVec->floating.x = posVec->integer.x * output.scaleVector.floating.x;
                posVec->floating.y = posVec->integer.y * output.scaleVector.floating.x;
                posVec->floating.z = posVec->integer.z * output.scaleVector.floating.x;
                posVec->floating.w = posVec->integer.w * output.scaleVector.floating.x;
                uvVec->floating.x = uvVec->integer.x * output.scaleVector.floating.y;
                uvVec->floating.y = uvVec->integer.y * output.scaleVector.floating.y;
                uvVec->floating.z = uvVec->integer.z * output.scaleVector.floating.y;
                uvVec->floating.w = uvVec->integer.w * output.scaleVector.floating.y;

                vertex.position = (Vector3) { .x = posVec->floating.x, .y = posVec->floating.y, .z = posVec->floating.z };
                vertex.uv = (Vector2) { .x = uvVec->floating.x, .y = uvVec->floating.y };
                vertex.aoUv = (Vector2) { .x = uvVec->floating.z, .y = uvVec->floating.w };
                vertex.emit = (Vector4) { .x = posVec->floating.w, .y = posVec->floating.w, .z = posVec->floating.w, .w = 1.0f };
                vertex.color = (Vector4) { .x = colorVec->integer.x / 255.0f, .y = colorVec->integer.y / 255.0f, .z = colorVec->integer.z / 255.0f, .w = colorVec->integer.w / 255.0f };
                
                TwinStudio_VIFVector* jointWeightVec = output.jointWeights + k;
                const uint32_t weightsAmount = jointWeightVec->integer.w & 0xFF;
                float weights[3] = { 0.0f, 0.0f, 0.0f };
                uint32_t joints[3] = { 0, 0, 0 };
                if (weightsAmount > 0)
                {
                    joints[0] = jointWeightVec->integer.x & 0x1FF;
                    joints[0] /= 4;
                    jointWeightVec->integer.x &= 0xFFFFFE00;
                    weights[0] = jointWeightVec->floating.x;
                }
                if (weightsAmount > 1)
                {
                    joints[1] = jointWeightVec->integer.y & 0x1FF;
                    joints[1] /= 4;
                    jointWeightVec->integer.y &= 0xFFFFFE00;
                    weights[1] = jointWeightVec->floating.y;
                }
                if (weightsAmount > 2)
                {
                    joints[2] = jointWeightVec->integer.z & 0x1FF;
                    joints[2] /= 4;
                    jointWeightVec->integer.z &= 0xFFFFFE00;
                    weights[2] = jointWeightVec->floating.z;
                }

                memcpy(vertex.jointIndices, joints, sizeof(joints));
                memcpy(vertex.weights, weights, sizeof(weights));

                for (size_t l = 0; l < blendModelPart->morphsAmount; ++l)
                {
                    uint8_t vifScratch[8192];
                    TwinRes_BlendSkinModelPartMorph* morph = blendModelPart->morphs + l;
                    TwinStudio_VIFInstruction instruction = { 0 };
                    instruction.fullInstruction = (morph->vertexesAmount << 0x10) | 0x6E000000;
                    instruction.unpack.addr = 0x7;
                    TwinStudio_DmaTag dma = { .qwc = morph->faceVifDataLength, .extra = instruction.fullInstruction };
                    TwinStudio_BinarySerializer* vifWriter = TwinStudio_BinSerializerAllocate(vifScratch, TwinStudio_BinarySerializerModeWrite, sizeof(vifScratch), false);
                    TwinStudio_BinWriteAny(vifWriter, &dma, sizeof(TwinStudio_DmaTag));
                    TwinStudio_BinWriteBlob(vifWriter, morph->vifData, morph->faceVifDataLength << 4);

                    TwinStudio_VIFOutput morphOutput = TwinStudio_VIFInterpretData(vifScratch, TwinStudio_BinGetStreamPosition(vifWriter), arena);
                    TwinStudio_BinSerializerFree(vifWriter);

                    for (size_t h = 0; h < arrlen(morphOutput.blendFaceOffsets); ++h)
                    {
                        Vector3 resVec = { 0 };
                        resVec.x = (int32_t)morphOutput.blendFaceOffsets[h].integer.x * blendModelPart->blendShape.x;
                        resVec.y = (int32_t)morphOutput.blendFaceOffsets[h].integer.y * blendModelPart->blendShape.y;
                        resVec.z = (int32_t)morphOutput.blendFaceOffsets[h].integer.z * blendModelPart->blendShape.z;
                        arrput(vertex.morphOffsets, resVec);
                    }
                }

                indexMap[indexMapIdx++] = TwinStudio_MeshAddVertex(&mesh, vertex);
                assert(indexMapIdx <= BLEND_SKIN_INDEX_MAP_SIZE);
            }

            bool winding = false;
            for (size_t k = 2; k < arrlen(output.vertexes); ++k)
            {
                const size_t startingOffset = indexMapIdx - arrlen(output.vertexes);
                TwinStudio_VIFVector* jointWeightVec = output.jointWeights + k;
                const bool skipsDraw = ((jointWeightVec->integer.w & 0xFF00) >> 8) == 128;
                if (skipsDraw)
                {
                    winding = !winding;
                    continue;
                }

                int32_t indices[3];
                if (!winding)
                {
                    indices[0] = startingOffset + k - 2;
                    indices[1] = startingOffset + k - 1;
                    indices[2] = startingOffset + k;
                }
                else
                {
                    indices[0] = startingOffset + k - 1;
                    indices[1] = startingOffset + k - 2;
                    indices[2] = startingOffset + k;
                }

                TwinStudio_MeshAddFaceI(&mesh, indexMap[indices[0]], indexMap[indices[1]], indexMap[indices[2]], i);
                winding = !winding;
            }
        }
    }

    renderBody->blendSkin = mesh;
}


static void BuildSkinMesh(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_RenderBody* renderBody, TwinRes_Skin* skin, TwinStudio_Arena* arena)
{
    TwinStudio_Mesh mesh = { 0 };
    for (size_t i = 0; i < arrlen(skin->skinParts); ++i)
    {
        TwinRes_SkinPart* skinPart = skin->skinParts + i;
        TwinRes_Material* material = TwinStudio_GetChunkResource(chunkRes, skinPart->materialLinkType, TS_SRT_None, skinPart->material).data;
        TwinStudio_VIFOutput output = TwinStudio_VIFInterpretData(skinPart->vifData, skinPart->vifDataLength, arena);
        int32_t indexMap[8192];

        TwinStudio_Material studioMaterial = { 0 };
        studioMaterial.name = TwinStudio_CopyFromCString(material->name.string);
        for (size_t k = 0; k < arrlen(material->shaders); ++k)
        {
            TwinRes_MaterialShader* shader = material->shaders + k;
            TwinRes_Texture* texture = TwinStudio_GetChunkResource(chunkRes, shader->textureLinkType, TS_SRT_None, shader->texture).data;
            arrput(studioMaterial.textures, texture->textureData);
        }
        arrput(mesh.materials, studioMaterial);

        for (size_t j = 0; j < arrlen(output.vertexes); ++j)
        {
            TwinStudio_MeshVertex vertex = { 0 };
            TwinStudio_VIFVector* posVec = output.vertexes + j;
            TwinStudio_VIFVector* uvVec  = output.uvColors + j;
            TwinStudio_VIFVector* colorVec = output.emits + j;
            posVec->floating.x = posVec->integer.x * output.scaleVector.floating.x;
            posVec->floating.y = posVec->integer.y * output.scaleVector.floating.x;
            posVec->floating.z = posVec->integer.z * output.scaleVector.floating.x;
            posVec->floating.w = posVec->integer.w * output.scaleVector.floating.x;
            uvVec->floating.x = uvVec->integer.x * output.scaleVector.floating.y;
            uvVec->floating.y = uvVec->integer.y * output.scaleVector.floating.y;
            uvVec->floating.z = uvVec->integer.z * output.scaleVector.floating.y;
            uvVec->floating.w = uvVec->integer.w * output.scaleVector.floating.y;

            vertex.position = (Vector3) { .x = posVec->floating.x, .y = posVec->floating.y, .z = posVec->floating.z };
            vertex.uv = (Vector2) { .x = uvVec->floating.x, .y = uvVec->floating.y };
            vertex.aoUv = (Vector2) { .x = uvVec->floating.z, .y = uvVec->floating.w };
            vertex.emit = (Vector4) { .x = posVec->floating.w, .y = posVec->floating.w, .z = posVec->floating.w, .w = 1.0f };
            vertex.color = (Vector4) { .x = colorVec->integer.x / 255.0f, .y = colorVec->integer.y / 255.0f, .z = colorVec->integer.z / 255.0f, .w = colorVec->integer.w / 255.0f };
            
            TwinStudio_VIFVector* jointWeightVec = output.jointWeights + j;
            const uint32_t weightsAmount = jointWeightVec->integer.w & 0xFF;
            float weights[3] = { 0.0f, 0.0f, 0.0f };
            uint32_t joints[3] = { 0, 0, 0 };
            if (weightsAmount > 0)
            {
                joints[0] = jointWeightVec->integer.x & 0x1FF;
                joints[0] /= 4;
                jointWeightVec->integer.x &= 0xFFFFFE00;
                weights[0] = jointWeightVec->floating.x;
            }
            if (weightsAmount > 1)
            {
                joints[1] = jointWeightVec->integer.y & 0x1FF;
                joints[1] /= 4;
                jointWeightVec->integer.y &= 0xFFFFFE00;
                weights[1] = jointWeightVec->floating.y;
            }
            if (weightsAmount > 2)
            {
                joints[2] = jointWeightVec->integer.z & 0x1FF;
                joints[2] /= 4;
                jointWeightVec->integer.z &= 0xFFFFFE00;
                weights[2] = jointWeightVec->floating.z;
            }

            memcpy(vertex.jointIndices, joints, sizeof(joints));
            memcpy(vertex.weights, weights, sizeof(weights));

            indexMap[j] = TwinStudio_MeshAddVertex(&mesh, vertex);
        }

        bool winding = false;
        for (size_t j = 2; j < arrlen(output.vertexes); ++j)
        {
            TwinStudio_VIFVector* jointWeightVec = output.jointWeights + j;
            const bool skipsDraw = ((jointWeightVec->integer.w & 0xFF00) >> 8) == 128;
            if (skipsDraw)
            {
                winding = !winding;
                continue;
            }

            int32_t indices[3];
            if (!winding)
            {
                indices[0] = j - 2;
                indices[1] = j - 1;
                indices[2] = j;
            }
            else
            {
                indices[0] = j - 1;
                indices[1] = j - 2;
                indices[2] = j;
            }

            TwinStudio_MeshAddFaceI(&mesh, indexMap[indices[0]], indexMap[indices[1]], indexMap[indices[2]], i);
            winding = !winding;
        }
    }

    renderBody->skin = mesh;
}


static void BuildRigidModelMesh(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_RenderBody* renderBody, uint8_t jointIndex, TwinRes_RigidModel* rigid, TwinStudio_Arena* arena)
{
    TwinStudio_Mesh mesh = { 0 };
    TwinRes_Model* model = TwinStudio_GetChunkResource(chunkRes, rigid->modelLinkType, TS_SRT_None, rigid->model).data;
    for (size_t i = 0; i < arrlen(model->modelParts); ++i)
    {
        TwinRes_ModelPart* modelPart = model->modelParts + i;
        TwinRes_Material* material = TwinStudio_GetChunkResource(chunkRes, rigid->materialsLinkType, TS_SRT_None, rigid->materials[i]).data;
        TwinStudio_VIFOutput output = TwinStudio_VIFInterpretData(modelPart->vifData, modelPart->vifDataLength, arena);
        int32_t indexMap[8192];

        TwinStudio_Material studioMaterial = { 0 };
        studioMaterial.name = TwinStudio_CopyFromCString(material->name.string);
        for (size_t k = 0; k < arrlen(material->shaders); ++k)
        {
            TwinRes_MaterialShader* shader = material->shaders + k;
            TwinRes_Texture* texture = TwinStudio_GetChunkResource(chunkRes, shader->textureLinkType, TS_SRT_None, shader->texture).data;
            arrput(studioMaterial.textures, texture->textureData);
        }
        arrput(mesh.materials, studioMaterial);

        for (size_t j = 0; j < arrlen(output.vertexes); ++j)
        {
            TwinStudio_MeshVertex vertex = { 0 };
            TwinStudio_VIFVector* posVec = output.vertexes + j;
            TwinStudio_VIFVector* uvColorsVec  = output.uvColors + j;
            TwinStudio_VIFVector* emitVec = output.emits + j;
            TwinStudio_VIFVector* normalVec = output.normals + j;

            vertex.position = (Vector3) { .x = posVec->floating.x, .y = posVec->floating.y, .z = posVec->floating.z };
            uint8_t r = uvColorsVec->integer.x & 0xFF;
            uint8_t g = uvColorsVec->integer.y & 0xFF;
            uint8_t b = uvColorsVec->integer.z & 0xFF;
            uint8_t a = uvColorsVec->integer.w & 0xFF;
            uvColorsVec->integer.x &= 0xFFFFFF00;
            uvColorsVec->integer.y &= 0xFFFFFF00;
            uvColorsVec->integer.z &= 0xFFFFFF00;

            vertex.uv = (Vector2) { .x = uvColorsVec->floating.x, .y = uvColorsVec->floating.y };
            vertex.color = (Vector4) { .x = r / 255.0f, .y = g / 255.0f, .z = b / 255.0f, .w = a / 255.0f };
            if (normalVec - j)
            {
                vertex.normal = (Vector3) { .x = normalVec->floating.x, .y = normalVec->floating.y, .z = normalVec->floating.z };
            }
            if (emitVec - j)
            {
                vertex.emit = (Vector4) { .x = emitVec->integer.x / 255.0f, .y = emitVec->integer.y / 255.0f, .z = emitVec->integer.z / 255.0f, .w = emitVec->integer.w / 255.0f };
            }
            
            vertex.weights[0] = 1.0f;
            vertex.jointIndices[0] = jointIndex;

            indexMap[j] = TwinStudio_MeshAddVertex(&mesh, vertex);
        }

        bool winding = false;
        for (size_t j = 2; j < arrlen(output.vertexes); ++j)
        {
            TwinStudio_VIFVector* uvColorsVec = output.uvColors + j;
            const bool skipsDraw = ((uvColorsVec->integer.w & 0xFF00) >> 8) == 128;
            if (skipsDraw)
            {
                winding = !winding;
                continue;
            }

            int32_t indices[3];
            if (!winding)
            {
                indices[0] = j - 2;
                indices[1] = j - 1;
                indices[2] = j;
            }
            else
            {
                indices[0] = j - 1;
                indices[1] = j - 2;
                indices[2] = j;
            }
            winding = !winding;

            TwinStudio_MeshAddFaceI(&mesh, indexMap[indices[0]], indexMap[indices[1]], indexMap[indices[2]], i);
        }
    }

    arrput(renderBody->rigidBodies, mesh);
}


TwinStudio_RenderBody TwinStudio_ConvertPs2Body(TwinStudio_ChunkResourceManager* chunkRes, TwinRes_Body* twinBody, TwinStudio_Arena* arena)
{
    TwinStudio_RenderBody result = { 0 };

    TwinStudio_ChunkResource blendSkinRes = TwinStudio_GetChunkResource(chunkRes, twinBody->blendSkinLinkType, TS_SRT_None, twinBody->blendSkin);
    TwinStudio_ChunkResource skinRes = TwinStudio_GetChunkResource(chunkRes, twinBody->skinLinkType, TS_SRT_None, twinBody->skin);
    TwinStudio_ChunkResource* rigidResources = NULL;
    arrsetcap(rigidResources, twinBody->rigidModelsAmount);
    for (size_t i = 0; i < twinBody->rigidModelsAmount; ++i)
    {
        arrput(rigidResources, TwinStudio_GetChunkResource(chunkRes, twinBody->rigidModelsLinkType, TS_SRT_None, twinBody->rigidModels[i]));
    }

    TwinRes_BlendSkin* blendSkin = blendSkinRes.data;
    TwinRes_Skin* skin = skinRes.data;

    if (blendSkin)
    {
        BuildBlendSkinMesh(chunkRes, &result, blendSkin, arena);
    }

    if (skin)
    {
        BuildSkinMesh(chunkRes, &result, skin, arena);
    }

    for (size_t i = 0; i < arrlenu(rigidResources); ++i)
    {
        TwinRes_RigidModel* rigidModel = rigidResources[i].data;
        if (rigidModel)
        {
            BuildRigidModelMesh(chunkRes, &result, twinBody->jointIndices[i], rigidModel, arena);
        }
    }

    for (size_t i = 0; i < twinBody->exitPointsAmount; ++i)
    {
        TwinRes_ExitPoint* exitPoint = twinBody->exitPoints + i;
        TwinStudio_ExitPoint studioExitPoint = { .id = exitPoint->id, .parentJointIndex = exitPoint->jointId, .matrix = exitPoint->transform };
        arrput(result.exitPoints, studioExitPoint);
    }

    for (size_t i = 0; i < twinBody->jointsAmount; ++i)
    {
        TwinRes_Joint* joint = twinBody->joints + i;
        TwinStudio_Joint studioJoint = {
            .reactJointId = joint->reactJointId,
            .id = joint->id,
            .parentId = joint->parentId,
            .childrenAmount = joint->childrenAmount1,
            .localRotation = joint->localRotation,
            .localTranslation = joint->localTranslation,
            .worldTranslation = joint->worldTranslation,
            .additionalAnimationRotation = joint->additionalAnimationRotation
        };
        arrput(result.joints, studioJoint);
    }

    arrfree(rigidResources);

    return result;
}