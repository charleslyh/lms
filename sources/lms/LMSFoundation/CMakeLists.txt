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
    PacketSource.h
    PacketSource.cpp
    Decoder.h
    Decoder.cpp
    Render.h
    Render.cpp
    Player.h
    Player.cpp
    Logger.h
    Logger.cpp
    Buffer.h
    Buffer.cpp
)
