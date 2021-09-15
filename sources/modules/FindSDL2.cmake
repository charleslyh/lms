##
# Find SDL2 Libraries.
#
# Once done this will define
#   SDL2_FOUND       - System has the all required components.
#   SDL2_INCLUDE_DIR - Include directory necessary for using the required components headers.
#   SDL2_LIBRARY     - Link these to use the required ffmpeg components.
##

find_path(SDL2_INCLUDE_DIR SDL2/SDL.h)
find_library(SDL2_LIBRARY SDL2)

if (SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
  set(${SDL2_FOUND} TRUE)
endif (SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
