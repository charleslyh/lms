cmake_minimum_required(VERSION 3.13)

add_library(RuntimeSDL STATIC)
set_property(TARGET RuntimeSDL PROPERTY FOLDER "extensions")

target_include_directories(RuntimeSDL
  PRIVATE
    ${SDL2_INCLUDE_DIR}
)

target_sources(RuntimeSDL
  PRIVATE
    SDLApplication.h
    SDLRuntime.cpp
    SDLView.h
    SDLView.cpp
    SDLSpeaker.h
    SDLSpeaker.cpp
    SDLAudioResampler.h
    SDLAudioResampler.cpp
)
