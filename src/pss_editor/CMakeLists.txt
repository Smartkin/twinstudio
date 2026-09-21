cmake_minimum_required(VERSION 3.27)
project(twinpss C)
set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample)

add_executable(twinpss
    main.c
    pss_format.c
    pss_codec.c
    mp4_io.c)
target_compile_definitions(twinpss PUBLIC
    $<$<CONFIG:Debug>:_DEBUG>)
target_link_libraries(twinpss PUBLIC common twindescgenerator twindescs dependencies raylib PkgConfig::FFMPEG)
target_include_directories(twinpss PUBLIC ../../common)
target_include_directories(twinpss PUBLIC pss_editor)

target_compile_features(twinpss PRIVATE c_std_11)

if (MSVC)
    target_compile_options(twinpss PRIVATE ${MSVC_COMPILE_OPTIONS})
else()
    if (TWINSTUDIO_ASAN)
        target_compile_options(twinpss PRIVATE -fsanitize=address)
        target_link_options(twinpss PRIVATE -fsanitize=address)
    endif()
endif()

add_custom_command(TARGET twinpss POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../../resources
    ${CMAKE_CURRENT_BINARY_DIR}/resources)
