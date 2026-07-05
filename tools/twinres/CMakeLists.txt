cmake_minimum_required(VERSION 3.27)
project(twinres C)
set(CMAKE_C_STANDARD 99)

add_executable(twinres src/lexer.c src/parser.c src/main.c)

target_include_directories(twinres PUBLIC include)
target_link_libraries(twinres PUBLIC common raylib)