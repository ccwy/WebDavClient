cmake_minimum_required(VERSION 3.15)
project(WebDavClient C)

set(CMAKE_C_STANDARD 99)

set(TARGET_PLATFORM "WIN10" CACHE STRING "Target Windows Platform: WIN7 or WIN10")

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

# 区分平台传递宏定义，并【同时传递给 C 编译器和资源编译器 rc.exe】
if(TARGET_PLATFORM STREQUAL "WIN7")
    target_compile_definitions(WebDavClient PRIVATE TARGET_WIN7)
    set_source_files_properties(res/app.rc PROPERTIES COMPILE_DEFINITIONS "TARGET_WIN7")
else()
    target_compile_definitions(WebDavClient PRIVATE TARGET_WIN10)
    set_source_files_properties(res/app.rc PROPERTIES COMPILE_DEFINITIONS "TARGET_WIN10")
endif()

target_link_libraries(WebDavClient user32 gdi32 advapi32)