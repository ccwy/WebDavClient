cmake_minimum_required(VERSION 3.15)
project(WebDavClient C)

set(CMAKE_C_STANDARD 99)

set(TARGET_PLATFORM "WIN10" CACHE STRING "Target Windows Platform: WIN7 or WIN10")

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 核心：由 CMake 根据当前编译架构（x64 还是 x86）直接指定对应的资源文件
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(RC_FILE_PATH "res/app_x64.rc")
    message(STATUS ">>> Architecture: 64-bit (x64), using ${RC_FILE_PATH}")
else()
    set(RC_FILE_PATH "res/app_x86.rc")
    message(STATUS ">>> Architecture: 32-bit (x86), using ${RC_FILE_PATH}")
endif()

add_executable(WebDavClient WIN32
    src/main.c 
    src/logger.c
    src/logger.h
    src/i18n.c
    src/i18n.h
    src/deployment.c 
    src/deployment.h 
    src/rclone_manager.c 
    src/rclone_manager.h
    src/config.c 
    src/config.h 	
    ${RC_FILE_PATH} 
    res/resource.h
)

# 仅需处理平台的 TARGET 宏
if(TARGET_PLATFORM STREQUAL "WIN7")
    target_compile_definitions(WebDavClient PRIVATE TARGET_WIN7)
    set_source_files_properties(${RC_FILE_PATH} PROPERTIES COMPILE_DEFINITIONS "TARGET_WIN7")
else()
    target_compile_definitions(WebDavClient PRIVATE TARGET_WIN10)
    set_source_files_properties(${RC_FILE_PATH} PROPERTIES COMPILE_DEFINITIONS "TARGET_WIN10")
endif()

target_link_libraries(WebDavClient user32 gdi32 advapi32)