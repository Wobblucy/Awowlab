cmake_minimum_required(VERSION 3.12)
project(Parser LANGUAGES CXX)

# Build Parser as a static library
add_library(Parser STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Encounter/EncounterScanner.cpp
    "Include/Actor/ActorTable.h"
    "Include/Actor/ActorMap.h"
    "Include/Encounter/EncounterScanner.h"
    "Include/Encounter/EncounterSegment.h"
    "Include/Haste/HasteBuffTable.h"
    "Include/Haste/HasteTracker.h"
)

target_include_directories(Parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

# Link against Txttokenizer for lightweight encounter scanning
target_link_libraries(Parser PUBLIC Txttokenizer)

# Precompiled header for faster compilation (base PCH without Vulkan/ImGui)
target_precompile_headers(Parser PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch_base.h>"
)

# Add test executable (only if BUILD_TESTS is enabled)
if(BUILD_TESTS)
    add_executable(ParserTests
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ParserTests.cpp
        "Include/Actor/ActorTable.h"
        "Include/Actor/ActorMap.h"
    )

    target_include_directories(ParserTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Include
    )

    # Link tests against Parser library
    target_link_libraries(ParserTests PRIVATE Parser)
endif()
