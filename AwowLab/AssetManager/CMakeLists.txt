cmake_minimum_required(VERSION 3.12)
project(AssetManager LANGUAGES CXX)

add_library(AssetManager STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AssetResolver.cpp
)

target_include_directories(AssetManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(AssetManager PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
