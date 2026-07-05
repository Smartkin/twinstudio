cmake_minimum_required(VERSION 3.27)
project(twinstudio C)
set(CMAKE_C_STANDARD 99)

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
    GIT_TAG e7ee3db58451317fdca9b4f2c3e921c85acba98a
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

# TODO: Execute tools after building to autogen resource C structs, UI layouts, etc.



add_subdirectory(descs)
add_executable(twinstudio src/main.c)
target_link_libraries(twinstudio PUBLIC common twindescs dependencies raylib PS2ImageMakerStatic flecs::flecs_static)
target_include_directories(twinstudio PUBLIC common)



add_custom_command(TARGET twinstudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
    ${CMAKE_CURRENT_BINARY_DIR}/resources)