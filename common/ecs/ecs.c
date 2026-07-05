#include "ecs.h"
#include "flecs.h"


ecs_world_t* ecsWorld;


void TwinStudio_EcsInit()
{
    ecsWorld = ecs_init();
}