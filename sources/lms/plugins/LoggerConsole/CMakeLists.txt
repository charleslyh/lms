cmake_minimum_required(VERSION 3.13)

add_library(LoggerConsole STATIC)
set_property(TARGET LoggerConsole PROPERTY FOLDER "plugins")

target_sources(LoggerConsole
  PRIVATE
    ConsoleLogger.cpp
)
