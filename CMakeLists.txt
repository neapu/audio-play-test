cmake_minimum_required(VERSION 4.0)
project(qt_audio_test)

set(CMAKE_CXX_STANDARD 23)

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
    add_compile_definitions(SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif ()

set(EXE_NAME "QtAudioTest")

add_executable(${EXE_NAME} main.cpp
        Decoder.cpp
        Decoder.h
        AudioRenderer.cpp
        AudioRenderer.h)

add_subdirectory(logger)
add_subdirectory(miniaudio)
target_link_libraries(${EXE_NAME} PRIVATE logger miniaudio)

set(FFMPEG_INCLUDE_DIR ${FFMPEG_DIR}/include)
set(FFMPEG_LIB_DIR ${FFMPEG_DIR}/lib)
find_library(AVFORMAT_LIBRARY NAMES avformat PATHS ${FFMPEG_LIB_DIR})
message(STATUS "AVFORMAT_LIBRARY: ${AVFORMAT_LIBRARY}")
find_library(AVCODEC_LIBRARY NAMES avcodec PATHS ${FFMPEG_LIB_DIR})
message(STATUS "AVCODEC_LIBRARY: ${AVCODEC_LIBRARY}")
find_library(AVUTIL_LIBRARY NAMES avutil PATHS ${FFMPEG_LIB_DIR})
message(STATUS "AVUTIL_LIBRARY: ${AVUTIL_LIBRARY}")
find_library(SWSCALE_LIBRARY NAMES swscale PATHS ${FFMPEG_LIB_DIR})
message(STATUS "SWSCALE_LIBRARY: ${SWSCALE_LIBRARY}")
find_library(SWRESAMPLE_LIBRARY NAMES swresample PATHS ${FFMPEG_LIB_DIR})
message(STATUS "SWRESAMPLE_LIBRARY: ${SWRESAMPLE_LIBRARY}")

target_include_directories(${EXE_NAME} PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(${EXE_NAME} PRIVATE ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} ${SWRESAMPLE_LIBRARY} ${SWSCALE_LIBRARY})