cmake_minimum_required(VERSION 3.21)

# Build for both macOS architectures (Intel + Apple Silicon).
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)

project(AutoDeafen VERSION 2.0.0)

# This mod is macOS-only.
if (NOT APPLE)
    message(FATAL_ERROR "AutoDeafen is a macOS-only mod and cannot be built for this platform.")
endif()

# Sources: Discord IPC client + OAuth redirect server + gameplay hooks.
# Pure C++/POSIX — no Objective-C++ or system frameworks (no keystroke faking).
file(GLOB SOURCES
    src/*.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Standard Geode setup.
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
