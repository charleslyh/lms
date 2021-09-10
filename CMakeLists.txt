cmake_minimum_required(VERSION 3.11)

set(CMAKE_VERBOSE_MAKEFILE ON)

# 强制使用 c++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(LMS LANGUAGES CXX)

##
# Include module pkg-config for CMake.
##
INCLUDE(FindPkgConfig)

##
# CMAKE_MODULE_PATH:FILEPATH=./Modules
##
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/Modules)

find_package(FFMPEG REQUIRED)
find_package(SDL2 REQUIRED)

add_subdirectory(lms)
