cmake_minimum_required(VERSION 3.13)

add_library(SourceFFM STATIC)
set_property(TARGET SourceFFM PROPERTY FOLDER "extensions")

target_include_directories(SourceFFM
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(SourceFFM
  PRIVATE
    FFVideoFile.h
    FFVideoFile.cpp
)
