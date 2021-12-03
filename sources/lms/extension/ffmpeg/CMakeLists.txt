cmake_minimum_required(VERSION 3.13)

add_library(ext_ffmpeg STATIC)

target_include_directories(ext_ffmpeg
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(ext_ffmpeg
  PRIVATE
    FFVideoFile.h
    FFVideoFile.cpp
)
