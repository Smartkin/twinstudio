#ifndef TS_ECS_H
#define TS_ECS_H

#define FLECS_CUSTOM_BUILD
#define FLECS_ALERTS         /**< Monitor conditions for errors. */
#define FLECS_C           /**< C API convenience macros, always enabled. */
#define FLECS_DOC            /**< Document entities and components. */
#define FLECS_JOURNAL     /**< Journaling addon. */
#define FLECS_JSON           /**< Parsing JSON to/from component values. */
#define FLECS_LOG            /**< When enabled, ECS provides more detailed logs. */
#define FLECS_META           /**< Reflection support. */
#define FLECS_METRICS        /**< Expose component data as statistics. */
#define FLECS_MODULE         /**< Module support. */
#define FLECS_OS_API_IMPL    /**< Default implementation for OS API. */
// #define FLECS_PERF_TRACE  /**< Enable performance tracing. */
#define FLECS_PIPELINE       /**< Pipeline support. */
#define FLECS_SYSTEM         /**< System support. */
#define FLECS_STATS          /**< Track runtime statistics. */
#define FLECS_TIMER          /**< Timer support. */
#define FLECS_UNITS          /**< Built-in standard units. */
#include <flecs.h>


void TwinStudio_EcsInit();

extern ecs_world_t* ecsWorld;

#endif // TS_ECS_H