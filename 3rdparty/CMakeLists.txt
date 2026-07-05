cmake_minimum_required(VERSION 3.27)
project(dependencies C)
set(CMAKE_C_STANDARD 99)

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE .)
target_sources(dependencies INTERFACE rpmalloc.c malloc.c cJSON.c cJSON_Utils.c impls.c)