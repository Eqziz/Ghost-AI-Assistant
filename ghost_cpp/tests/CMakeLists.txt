cmake_minimum_required(VERSION 3.15)
project(StressTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_executable(stress_test
    stress_config_crypto.cpp
    ../src/config.cpp
    ../src/crypto.cpp
    ../src/network_logger.cpp
)

target_link_libraries(stress_test PRIVATE
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
    dbghelp
)
