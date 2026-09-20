cmake_minimum_required(VERSION 3.27)
project(twinmusic C)
set(CMAKE_C_STANDARD 11)

add_executable(twinmusic main.c)
target_compile_definitions(twinmusic PUBLIC
    $<$<CONFIG:Debug>:_DEBUG>)
target_link_libraries(twinmusic PUBLIC common twindescgenerator twindescs dependencies raylib)
target_include_directories(twinmusic PUBLIC ../../common)
target_include_directories(twinmusic PUBLIC music_manager)

target_compile_features(twinmusic PRIVATE c_std_11)

if (MSVC)
    target_compile_options(twinmusic PRIVATE ${MSVC_COMPILE_OPTIONS})
else()
    if (TWINSTUDIO_ASAN)
        target_compile_options(twinmusic PRIVATE -fsanitize=address)
        target_link_options(twinmusic PRIVATE -fsanitize=address)
    endif()
endif()

add_custom_command(TARGET twinmusic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../../resources
    ${CMAKE_CURRENT_BINARY_DIR}/resources)
