cmake_minimum_required(VERSION 3.12)
project(MMapManager LANGUAGES CXX)

add_library(MMapManager STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MMapManager.cpp
)

target_include_directories(MMapManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
