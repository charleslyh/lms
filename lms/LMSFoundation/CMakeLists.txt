add_library(LMSFoundation STATIC)

target_include_directories(LMSFoundation
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_sources(LMSFoundation
  PRIVATE
    Foundation.cpp
    Player.cpp
)