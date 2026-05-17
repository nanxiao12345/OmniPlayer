cmake_minimum_required(VERSION 3.20)
project(MediaPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia OpenGLWidgets)
find_package(OpenGL REQUIRED)

# Default to known FFmpeg source/install path
set(FFMPEG_ROOT "D:/Tools/ffmpeg-shared" CACHE PATH "FFmpeg install root (contains include/, bin/ with .lib)")
list(APPEND CMAKE_PREFIX_PATH "${FFMPEG_ROOT}")

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    PATHS "${FFMPEG_ROOT}/include"
    NO_DEFAULT_PATH)
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})

# MSVC .lib files may be in bin/ or lib/
foreach(comp avcodec avformat avutil swresample swscale avfilter)
    find_library(FFMPEG_${comp}_LIBRARY ${comp}
        PATHS "${FFMPEG_ROOT}/bin" "${FFMPEG_ROOT}/lib"
        NO_DEFAULT_PATH)
    if(NOT FFMPEG_${comp}_LIBRARY)
        message(FATAL_ERROR "Could not find ${comp}.lib in ${FFMPEG_ROOT}/bin or ${FFMPEG_ROOT}/lib")
    endif()
    list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${comp}_LIBRARY})
endforeach()

add_subdirectory(src)
