cmake_minimum_required(VERSION 3.13)

add_library(LMSFoundation STATIC)

target_include_directories(LMSFoundation
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(LMSFoundation
  PRIVATE
    Foundation.h
    Foundation.cpp
    Player.h
    Player.cpp
    Decoder.h
    Decoder.cpp
    Render.h
    Render.cpp

    PacketSource.h
)
