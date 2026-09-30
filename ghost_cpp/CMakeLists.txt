cmake_minimum_required(VERSION 3.15)
project(GhostAI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(SOURCES
    src/anti_analysis.cpp
    src/crypto.cpp
    src/license_manager.cpp
    src/config.cpp
    src/conversation.cpp
    src/capture_area.cpp
    src/screen_capture.cpp
    src/vision_client.cpp
    src/keybinder.cpp
    src/ghost_ui.cpp
    src/mode_selector.cpp
    src/activation_ui.cpp
    src/setup_ui.cpp
    src/ghost_assistant.cpp
    src/main.cpp
)

add_executable(GhostAI WIN32 ${SOURCES})

target_link_libraries(GhostAI PRIVATE
    gdiplus
    crypt32
    bcrypt
    winhttp
    comctl32
    ole32
    user32
    gdi32
    shell32
    advapi32
)
