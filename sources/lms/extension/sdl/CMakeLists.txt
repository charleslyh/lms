cmake_minimum_required(VERSION 3.13)

add_library(ext_sdl STATIC)

target_include_directories(ext_sdl
  PRIVATE
    ${SDL2_INCLUDE_DIR}
)

target_sources(ext_sdl
  PRIVATE
    SDLApplication.h
    SDLRuntime.cpp
    SDLView.h
    SDLView.cpp
)
