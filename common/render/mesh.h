#ifndef TS_MESH_H
#define TS_MESH_H


#include <raylib.h>
#include <stdint.h>


typedef enum {
    TwinStudio_MT_RigidModel,
    TwinStudio_MT_Skin,
    TwinStudio_MT_BlendSkin,
} TwinStudio_MeshType;


typedef struct TwinStudio_Material {
    void** textures;
} TwinStudio_Material;


typedef struct TwinStudio_MeshVertex {
    Vector3 position;
    Vector2 uv;
    Vector2 aoUv;
    Vector3 normal;
    Vector4 color;
    Vector4 emit;
    Vector3* morphOffsets;
    float  weights[3];
    int32_t jointIndices[3];
    union {
        uint8_t presentFields;
        struct {
            uint8_t hasNormals : 1;
            uint8_t hasEmits : 1;
            uint8_t hasMorphOffsets : 1;
            uint8_t isSkin : 1;
        };
    };
} TwinStudio_MeshVertex;

typedef struct TwinStudio_MeshFace {
    TwinStudio_MeshVertex vertexes[3];
    TwinStudio_Material material;
} TwinStudio_MeshFace;

typedef struct TwinStudio_Mesh {
    TwinStudio_MeshFace* faces;
} TwinStudio_Mesh;

typedef struct TwinStudio_Animation {
} TwinStudio_Animation;

typedef struct TwinStudio_Joint {
} TwinStudio_Joint;

typedef struct TwinStudio_ExitPoint {
    uint32_t id;
    uint32_t parentJointIndex;
    Matrix matrix;
} TwinStudio_ExitPoint;

typedef struct TwinStudio_RenderBody {
    TwinStudio_Mesh* blendSkin;
    TwinStudio_Mesh* skin;
    TwinStudio_Mesh** rigidBodies;
    TwinStudio_Joint* joints;
    TwinStudio_ExitPoint* exitPoints;
    TwinStudio_Animation** animations;
} TwinStudio_RenderBody;


#endif // TS_MESH_H