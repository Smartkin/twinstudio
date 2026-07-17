#ifndef TS_RESOURCES_H
#define TS_RESOURCES_H

#include <raylib.h>

typedef enum {
    TS_RT_Texture = 1 << 0,
    TS_RT_Mesh = 1 << 1,
    TS_RT_Lod = 1 << 2,
    TS_RT_RigidModel = 1 << 3,
    TS_RT_Sound = 1 << 4,
    TS_RT_Material = 1 << 5,
    TS_RT_Model = 1 << 6,
    TS_RT_Skin = 1 << 7,
    TS_RT_BlendSkin = 1 << 8,
    TS_RT_Chunk = 1 << 9,
    TS_RT_Animation = 1 << 10,
    TS_RT_Object = 1 << 11,
    TS_RT_Behavior = 1 << 12,
    TS_RT_SequenceBehavior = 1 << 13,
    TS_RT_Instance = 1 << 14,
    TS_RT_Path = 1 << 15,
    TS_RT_Camera = 1 << 16,
    TS_RT_Trigger = 1 << 17,
    TS_RT_Skydome = 1 << 18,
    TS_RT_CollisionSurface = 1 << 19,
    TS_RT_Position = 1 << 20,
    TS_RT_AI_Position = 1 << 21,
    TS_RT_AI_Path = 1 << 22,
    TS_RT_Body = 1 << 23,
    TS_RT_ParticleSystem = 1 << 24,
    TS_RT_SequenceBehaviorAction = 1 << 25,
} TwinStudio_ResourceType;

#endif // TS_RESOURCES_H