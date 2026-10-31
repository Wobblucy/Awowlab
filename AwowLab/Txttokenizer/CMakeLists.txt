cmake_minimum_required(VERSION 3.12)
project(Txttokenizer LANGUAGES CXX)

# Build tokenizer as a static library
add_library(Txttokenizer STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Txttokenizer.cpp
)

target_include_directories(Txttokenizer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

