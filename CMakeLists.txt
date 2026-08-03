cmake_minimum_required(VERSION 3.12)
project(EmbeddedWebDavClient C)

set(CMAKE_C_STANDARD 99)

add_executable(EmbeddedWebDavClient WIN32
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

target_link_libraries(EmbeddedWebDavClient user32 gdi32 advapi32)