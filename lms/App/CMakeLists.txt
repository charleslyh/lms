cmake_minimum_required(VERSION 3.11)

add_executable(player)

target_include_directories(player
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIR}
)

target_link_libraries(player
  PRIVATE
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARY}
    LMSFoundation
)

target_sources(player
  PRIVATE
    SDLApplication.cpp
)

target_sources(player
  PRIVATE
    AppDelegate.cpp
)
