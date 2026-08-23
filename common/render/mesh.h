#ifndef TS_MESH_H
#define TS_MESH_H


#include <raylib.h>
#include <stdint.h>
#include <string_view/string_view.h>


typedef enum {
    TwinStudio_MT_RigidModel,
    TwinStudio_MT_Skin,
    TwinStudio_MT_BlendSkin,
} TwinStudio_MeshType;


typedef struct TwinStudio_Material {
    Image* textures;
    TwinStudio_StringView name;
} TwinStudio_Material;


typedef struct TwinStudio_MeshVertex {
    Vector3 position;
    Vector2 uv;
    Vector2 aoUv;
    Vector3 normal;
    Vector4 color;
    Vector4 emit;
    Vector3* morphOffsets;
    float   weights[3];
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
    uint32_t idx[3];
    uint32_t materialIdx;
} TwinStudio_MeshFace;

typedef struct TwinStudio_Mesh {
    TwinStudio_MeshFace* faces;
    TwinStudio_MeshVertex* vertexes;
    TwinStudio_Material* materials;
} TwinStudio_Mesh;

typedef struct TwinStudio_Animation {
    Vector3** positionSpline; // per joint
    Vector3** scaleSpline; // per joint
    Quaternion** rotationSpline; // per joint
    float** morphSpline; // per morph face
} TwinStudio_Animation;

typedef struct TwinStudio_Joint {
    uint32_t reactJointId;
    uint32_t id;
    uint32_t parentId;
    uint32_t childrenAmount;
    Vector4  localTranslation;
    Vector4  worldTranslation;
    Quaternion localRotation;
    Quaternion additionalAnimationRotation;
} TwinStudio_Joint;

typedef struct TwinStudio_ExitPoint {
    uint32_t id;
    uint32_t parentJointIndex;
    Matrix matrix;
} TwinStudio_ExitPoint;

typedef struct TwinStudio_RenderBody {
    TwinStudio_Mesh blendSkin;
    TwinStudio_Mesh skin;
    TwinStudio_Mesh* rigidBodies;
    TwinStudio_Joint* joints;
    TwinStudio_ExitPoint* exitPoints;
    TwinStudio_Animation** animations;
} TwinStudio_RenderBody;


int32_t TwinStudio_MeshAddVertex(TwinStudio_Mesh* mesh, TwinStudio_MeshVertex v);
void TwinStudio_MeshAddFace(TwinStudio_Mesh* mesh, TwinStudio_MeshVertex v1, TwinStudio_MeshVertex v2, TwinStudio_MeshVertex v3, uint32_t materialIndex);
void TwinStudio_MeshAddFaceI(TwinStudio_Mesh* mesh, int32_t idx1, int32_t idx2, int32_t idx3, uint32_t materialIndex);

#endif // TS_MESH_H