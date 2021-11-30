cmake_minimum_required(VERSION 3.13)

add_library(lms STATIC)

target_include_directories(lms
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(lms
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
    Packet.cpp
    Packet.h
    Runtime.h
    
    RuntimeInternal.cpp
    RuntimeInternal.h

    # 3rd-party libraries
    Stacktrace.h
)

target_link_libraries(lms ext_sdl)
