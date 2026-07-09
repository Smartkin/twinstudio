cmake_minimum_required(VERSION 3.27)
project(twinstudio C)
set(CMAKE_C_STANDARD 11)

set(TWINSTUDIO_ASAN FALSE) # Turn on address sanitizer

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE) # don't build raylib examples
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE) # don't build raylib example games

fetchcontent_declare(
    raylib
    GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
    GIT_TAG "6.0"
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)

fetchcontent_declare(
    ps2imagemaker
    GIT_REPOSITORY "https://github.com/Smartkin/PS2ImageMaker.git"
    GIT_TAG "v0.5"
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)

fetchcontent_declare(
    flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG "v4.1.5"
    GIT_PROGRESS TRUE
    GIT_SHALLOW TRUE
)
 
fetchcontent_makeavailable(flecs)
fetchcontent_makeavailable(raylib)
fetchcontent_makeavailable(ps2imagemaker)

if (TWINSTUDIO_ASAN)
    add_compile_definitions(TWIN_ASAN)
endif()

add_subdirectory(3rdparty)
add_subdirectory(common)
add_subdirectory(tools)

# Generate serialization desc files. TODO: UI layouts later
file(GLOB_RECURSE TWIN_DESCS ${CMAKE_CURRENT_SOURCE_DIR}/descs/*.tr)
set(GENERATED_DESCS "")
foreach(DESC ${TWIN_DESCS})
    get_filename_component(NAME ${DESC} NAME_WE)
    get_filename_component(OUT_PATH ${DESC} DIRECTORY)
    set(OUT ${OUT_PATH}/auto_struct_${NAME}.c)
    add_custom_command(
        OUTPUT ${OUT} ${OUT_PATH}/auto_struct_${NAME}.h
        COMMAND twinres ${DESC}
        DEPENDS twinres ${DESC}
        VERBATIM
    )
    list(APPEND GENERATED_DESCS ${OUT})
endforeach()
add_library(twindescgenerator INTERFACE ${GENERATED_DESCS})
add_subdirectory(descs)
add_dependencies(twindescs twindescgenerator)

add_executable(twinstudio src/main.c)
target_link_libraries(twinstudio PUBLIC common twindescgenerator twindescs dependencies raylib PS2ImageMakerStatic flecs::flecs_static)
target_include_directories(twinstudio PUBLIC common)

target_compile_features(twinstudio PRIVATE c_std_11)

if (MSVC)
    set(MSVC_COMPILE_OPTIONS /experimental:c11atomics)
    if (TWINSTUDIO_ASAN)
        set(MSVC_COMPILE_OPTIONS ${MSVC_COMPILE_OPTIONS} /fsanitize=address)
    endif()
    target_compile_options(twinstudio PRIVATE ${MSVC_COMPILE_OPTIONS})
else()
    if (TWINSTUDIO_ASAN)
        target_compile_options(twinstudio PRIVATE -fsanitize=address)
        target_link_options(twinstudio PRIVATE -fsanitize=address)
    endif()
endif()


add_custom_command(TARGET twinstudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/resources)