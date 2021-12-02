cmake_minimum_required(VERSION 3.13)

add_library(lms STATIC)

target_include_directories(lms
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(lms
  PUBLIC
    Buffer.h
    Decoder.h
    Foundation.h
    Logger.h
    MediaSource.h
    Packet.h
    Player.h
    Render.h
    Runtime.h

  PRIVATE
    Runtime-.h

  PRIVATE
    Buffer.cpp
    Decoder.cpp
    Foundation.cpp
    Logger.cpp
    MediaSource.cpp
    Packet.cpp
    Player.cpp
    Render.cpp
    Runtime.cpp
)

# 3rd-party libraries
target_sources(lms
  PRIVATE
    Stacktrace.h
)

# 具体link哪些
target_link_libraries(lms ext_sdl)
