cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GDAI VERSION 1.0.0)

# Geode SDK
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# GDReplayFormat для .gdr2
include(cmake/CPM.cmake)
CPMAddPackage(
    NAME GDReplayFormat
    GITHUB_REPOSITORY maxnut/GDReplayFormat
    GIT_TAG 7c3c9d2
)

target_link_libraries(${PROJECT_NAME} libGDR)

setup_geode_mod(${PROJECT_NAME})
