cmake_minimum_required(VERSION 3.27)
project(dependencies C)
set(CMAKE_C_STANDARD 11)

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE .)
target_sources(dependencies INTERFACE rpmalloc.c malloc.c cJSON.c cJSON_Utils.c tinyfiledialogs.c impls.c)
target_compile_features(dependencies INTERFACE c_std_11)

if (MSVC)
    target_compile_options(dependencies INTERFACE /experimental:c11atomics)
endif()