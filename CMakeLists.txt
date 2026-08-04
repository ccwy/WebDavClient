cmake_minimum_required(VERSION 3.15)
project(WebDavClient C)

set(CMAKE_C_STANDARD 99)

# 平台选项：支持 WIN7 或 WIN10，默认打包 WIN10
set(TARGET_PLATFORM "WIN10" CACHE STRING "Target Windows Platform: WIN7 or WIN10")

# 开启 MSVC 静态链接运行库 (/MT /MTd)，确保无须外部 DLL 即可直接运行
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
    res/app.rc 
    res/resource.h
)

# 根据目标平台传递编译宏定义
if(TARGET_PLATFORM STREQUAL "WIN7")
    target_compile_definitions(WebDavClient PRIVATE TARGET_WIN7)
else()
    target_compile_definitions(WebDavClient PRIVATE TARGET_WIN10)
endif()

target_link_libraries(WebDavClient user32 gdi32 advapi32)