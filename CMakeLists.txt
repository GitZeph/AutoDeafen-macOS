cmake_minimum_required(VERSION 3.21)

# Permette di compilare per entrambe le architetture macOS (Intel + Apple Silicon)
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)

project(AutoDeafen VERSION 1.0.0)

# ---------------------------------------------------------------------------
# Questa mod è ESPLICITAMENTE solo per macOS.
# ---------------------------------------------------------------------------
if (NOT APPLE)
    message(FATAL_ERROR "AutoDeafen is a macOS-only mod and cannot be built for this platform.")
endif()

# ---------------------------------------------------------------------------
# Sorgenti
# ---------------------------------------------------------------------------
# Backend Discord IPC + server OAuth + hook di gameplay. Tutto C++/POSIX puro:
# niente più Objective-C++ né framework di sistema (non simuliamo più tasti).
file(GLOB SOURCES
    src/*.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

# ---------------------------------------------------------------------------
# Setup standard di Geode
# ---------------------------------------------------------------------------
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
