cmake_minimum_required(VERSION 3.13)

add_executable(demo)

target_include_directories(demo
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/lms
)

target_link_libraries(demo
  PRIVATE
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARY}
    lms
    runtime_sdl
    filesrc_ffm
)

target_sources(demo
  PRIVATE
    AppDelegate.cpp
)
