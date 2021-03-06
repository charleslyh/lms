cmake_minimum_required(VERSION 3.13)

add_library(lms STATIC)

target_include_directories(lms
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(lms
  PRIVATE
    LoggerConsole
)

# lms对业务侧可暴露的头文件列表
list(APPEND interface_headers
  LMS.h
  Foundation.h
  Logger.h
  MediaSource.h
  Events.h
  Player.h
)

# lms内部实现文件列表
list(APPEND source_files
  Module.h
  
  Cell.h
  Cell.cpp

  Buffer.h
  Buffer.cpp

  Runtime.h
  Runtime.cpp

  Decoder.h
  Decoder.cpp
  
  Events.cpp
  
  SourceDriver.h
  SourceDriver.cpp
  
  TimeSync.h
  TimeSync.cpp
  
  VideoRenderDriver.h
  VideoRenderDriver.cpp
 
  LMS.cpp
  Foundation.cpp
  Logger.cpp
  MediaSource.cpp
  Player.cpp
)

# lms引用的第三方库源代码文件
# TODO: 使用更合理的方式管理第三方库文件
list(APPEND thirdparty_files
  Stacktrace.h
)

target_sources(lms
  PUBLIC  ${interface_headers}
  PRIVATE ${source_files}
  PRIVATE ${thirdparty_files}
)

source_group("Interface"  FILES ${interface_headers})
source_group("Sources"    FILES ${source_files})
source_group("ThirdParty" FILES ${thirdparty_files})
