#ifndef TS_RESOURCES_H
#define TS_RESOURCES_H

#include <raylib.h>

typedef enum {
    TS_RT_Texture,
    TS_RT_Mesh,
    TS_RT_Lod,
    TS_RT_RigidModel,
    TS_RT_Sound,
    TS_RT_Material,
    TS_RT_Model,
    TS_RT_Skin,
    TS_RT_BlendSkin,
    TS_RT_Chunk,
    TS_RT_Animation,
    TS_RT_Object,
    TS_RT_Behavior,
    TS_RT_SequenceBehavior,
    TS_RT_Instance,
    TS_RT_Path,
    TS_RT_Camera,
    TS_RT_Trigger,
    TS_RT_Skydome,
    TS_RT_CollisionSurface,
    TS_RT_Position,
    TS_RT_AI_Position,
    TS_RT_AI_Path,
    TS_RT_Body
} TwinStudio_ResourceType;

#endif // TS_RESOURCES_H