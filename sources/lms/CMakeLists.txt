cmake_minimum_required(VERSION 3.13)

include_directories(.)

add_subdirectory(lms)
add_subdirectory(plugins/LoggerConsole)

add_subdirectory(extension/Runtime+SDL)
add_subdirectory(extension/Source+FFM)
