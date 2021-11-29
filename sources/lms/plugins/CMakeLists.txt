cmake_minimum_required(VERSION 3.13)

add_library(plugins STATIC)

target_include_directories(plugins
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(plugins
  PUBLIC
    Runtime+SDL.h

  PRIVATE
    Runtime+SDL.cpp
)
