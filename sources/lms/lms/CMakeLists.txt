cmake_minimum_required(VERSION 3.13)

add_library(lms STATIC)

target_include_directories(lms
  PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
)

# lms对业务侧可暴露的头文件列表
list(APPEND interface_headers
  Foundation.h
  Logger.h
  Render.h
  MediaSource.h
  Player.h
)

# lms内部实现文件列表
list(APPEND implement_files
  Buffer.h
  Buffer.cpp

  Runtime.h
  Runtime.cpp

  Decoder.h
  Decoder.cpp

  Packet.h
  Packet.cpp

  Foundation.cpp
  Logger.cpp
  MediaSource.cpp
  Player.cpp
  Render.cpp
)

# lms引用的第三方库源代码文件
# TODO: 使用更合理的方式管理第三方库文件
list(APPEND thirdparty_files
  Stacktrace.h
)

target_sources(lms
  PUBLIC ${interface_headers}
  PRIVATE ${implement_files}
  PRIVATE ${thirdparty_files}
)

source_group("Interface"  FILES ${interface_headers})
source_group("Internal"   FILES ${implement_files})
source_group("ThirdParty" FILES ${thirdparty_files})
