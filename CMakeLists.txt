cmake_minimum_required(VERSION 3.12)
project(EmbeddedWebDavClient C)

set(CMAKE_C_STANDARD 99)

# Define executable and sources
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
    res/app.rc 
    res/resource.h
)

# Link Windows core APIs
target_link_libraries(EmbeddedWebDavClient user32 gdi32 advapi32)

# Auto-copy the lang folder to the output directory for easy debugging
add_custom_command(TARGET EmbeddedWebDavClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/lang $<TARGET_FILE_DIR:EmbeddedWebDavClient>/lang
)