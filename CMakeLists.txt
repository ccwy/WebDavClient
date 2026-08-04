cmake_minimum_required(VERSION 3.15)
project(WebDavClient C)

set(CMAKE_C_STANDARD 99)

# 开启 MSVC 静态链接运行库 (/MT /MTd)，使得编译出的程序不再依赖任何外部 VC 运行库
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

target_link_libraries(WebDavClient user32 gdi32 advapi32)