cmake_minimum_required(VERSION 3.27)
project(common C)
set(CMAKE_C_STANDARD 11)

add_library(common STATIC ui/ui.c
    string_view/string_view.c
    timer/timer.c
    ui/textbox.c
    math/math.c
    ui/button.c
    memory/memory.c
    variant/variant.c
    serialization/binary_serializer.c
    serialization/json_serializer.c
    serialization/helpers.c
    ecs/ecs.c
    audio/wave.c
    audio/adpcm.c
    image/ez_swizzle.c
    ps2/gif_tag.c
    ps2/vif.c
    resources/resources.c
    resources/chunk_resources.c
    render/mesh.c
    gltf/gltf_io.c)
target_include_directories(common PUBLIC .)
target_link_libraries(common PUBLIC dependencies flecs::flecs_static raylib)
target_compile_features(common PRIVATE c_std_11)

if (MSVC)
    target_compile_options(common PRIVATE /experimental:c11atomics)
endif()