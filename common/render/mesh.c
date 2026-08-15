#include "mesh.h"
#include "raymath.h"
#include <assert.h>
#include <stb_ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <raylib.h>
#include <stdint.h>

static bool IsVertexEqual(TwinStudio_MeshVertex v1, TwinStudio_MeshVertex v2)
{
    if (!Vector3Equals(v1.position, v2.position))
    {
        return false;
    }

    if (!Vector2Equals(v1.uv, v2.uv))
    {
        return false;
    }

    if (!Vector3Equals(v1.normal, v2.normal))
    {
        return false;
    }

    if (!Vector4Equals(v1.color, v2.color))
    {
        return false;
    }

    if (!FloatEquals(v1.weights[0], v2.weights[0])
        || !FloatEquals(v1.weights[1], v2.weights[1])
        || !FloatEquals(v1.weights[2], v2.weights[2]))
    {
        return false;
    }

    if (v1.jointIndices[0] != v2.jointIndices[0]
        || v1.jointIndices[1] != v2.jointIndices[1]
        || v1.jointIndices[2] != v2.jointIndices[2])
    {
        return false;
    }

    if (!Vector4Equals(v1.emit, v2.emit))
    {
        return false;
    }

    if (!Vector2Equals(v1.aoUv, v2.aoUv))
    {
        return false;
    }

    if (v1.morphOffsets && v2.morphOffsets)
    {
        for (size_t i = 0; i < arrlen(v1.morphOffsets); ++i)
        {
            if (!Vector3Equals(v1.morphOffsets[i], v2.morphOffsets[i]))
            {
                return false;
            }
        }
    }

    return true;
}

static int32_t FindExistingVertex(TwinStudio_Mesh* mesh, TwinStudio_MeshVertex v)
{
    int32_t result = -1;

    for (size_t i = 0; i < arrlen(mesh->vertexes); ++i)
    {
        if (IsVertexEqual(mesh->vertexes[i], v))
        {
            return i;
        }
    }

    return result;
}


static bool IsDegenerate(int32_t i1, int32_t i2, int32_t i3)
{
    return i1 == i2 || i2 == i3 || i1 == i3;
}


int32_t TwinStudio_MeshAddVertex(TwinStudio_Mesh* mesh, TwinStudio_MeshVertex v)
{
    int32_t idx = FindExistingVertex(mesh, v);
    if (idx != -1)
    {
        return idx;
    }

    idx = arrlen(mesh->vertexes);
    arrput(mesh->vertexes, v);
    return idx;
}


void TwinStudio_MeshAddFace(TwinStudio_Mesh* mesh, TwinStudio_MeshVertex v1, TwinStudio_MeshVertex v2, TwinStudio_MeshVertex v3, TwinStudio_Material material)
{
    int32_t v1Idx = FindExistingVertex(mesh, v1);
    int32_t v2Idx = FindExistingVertex(mesh, v2);
    int32_t v3Idx = FindExistingVertex(mesh, v3);
    TwinStudio_MeshAddFaceI(mesh, v1Idx, v2Idx, v3Idx, material);
}


void TwinStudio_MeshAddFaceI(TwinStudio_Mesh* mesh, int32_t v1Idx, int32_t v2Idx, int32_t v3Idx, TwinStudio_Material material)
{
    TwinStudio_MeshFace newFace = { 0 };
    assert(v1Idx != -1);
    assert(v2Idx != -1);
    assert(v3Idx != -1);

    if (IsDegenerate(v1Idx, v2Idx, v3Idx))
    {
        return;
    }

    newFace.idx[0] = v1Idx;
    newFace.idx[1] = v2Idx;
    newFace.idx[2] = v3Idx;
    newFace.material = material;
    arrput(mesh->faces, newFace);
}