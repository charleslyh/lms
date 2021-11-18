cmake_minimum_required(VERSION 3.13)

add_library(LMSFoundation STATIC)

target_include_directories(LMSFoundation
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(LMSFoundation
  PRIVATE
    Buffer.cpp
    Buffer.h
    Decoder.cpp
    Decoder.h
    Foundation.cpp
    Foundation.h
    Logger.cpp
    Logger.h
    MediaSource.cpp
    MediaSource.h
    Player.cpp
    Player.h
    Render.cpp
    Render.h
    stacktrace.h
)
