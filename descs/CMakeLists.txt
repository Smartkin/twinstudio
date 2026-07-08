cmake_minimum_required(VERSION 3.27)
project(twindescs C)
set(CMAKE_C_STANDARD 11)

file(GLOB_RECURSE AUTO_GENERATED_FILES *.h *.c)

add_library(${PROJECT_NAME} INTERFACE ${AUTO_GENERATED_FILES}
    ps2/retail/graphics/texture_serializer.c)
target_include_directories(twindescs INTERFACE .)
target_link_libraries(twindescs INTERFACE common)