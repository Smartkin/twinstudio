cmake_minimum_required(VERSION 3.27)
project(twinstudio C)
set(CMAKE_C_STANDARD 11)

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

add_subdirectory(descs)
add_executable(twinstudio src/main.c)
# target_sources(twinstudio PRIVATE ${GENERATED_DESCS}) # TODO: Uncomment when most converters are done and manual reads are also done
target_link_libraries(twinstudio PUBLIC common twindescs dependencies raylib PS2ImageMakerStatic flecs::flecs_static)
target_include_directories(twinstudio PUBLIC common)

target_compile_features(twinstudio PRIVATE c_std_11)

if (MSVC)
    target_compile_options(twinstudio PRIVATE /experimental:c11atomics)
endif()


add_custom_command(TARGET twinstudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/resources)