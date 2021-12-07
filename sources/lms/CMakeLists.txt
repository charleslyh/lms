cmake_minimum_required(VERSION 3.13)

include_directories(.)

add_subdirectory(lms)
add_subdirectory(plugins/LoggerConsole)

add_subdirectory(extension/RuntimeSDL)
add_subdirectory(extension/SourceFFM)
