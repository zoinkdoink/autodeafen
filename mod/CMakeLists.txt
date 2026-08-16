cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

project(AutoDeafen VERSION 0.1.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/config_store.cpp
    src/deafen.cpp
    src/discord_rpc.cpp
    src/discord_setting.cpp
    src/level_config_popup.cpp
    src/rpc_conn.cpp
    src/keysender_win.cpp
    src/keysender_mac.cpp
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})

if (WIN32)
    target_link_libraries(${PROJECT_NAME} ws2_32)
endif()
if (APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework CoreGraphics")
endif()
