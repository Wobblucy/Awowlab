# RaidNotes module - ERT-compatible raid notes creation and export

find_package(RapidJSON CONFIG REQUIRED)

add_library(RaidNotes STATIC
    src/RaidNoteDocument.cpp
    src/RaidNoteExporter.cpp
    src/RaidNoteContextMenu.cpp
)

target_include_directories(RaidNotes PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Core/LocalizationManager.h; its .cpp is built into the app that links this
target_include_directories(RaidNotes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(RaidNotes PUBLIC imgui rapidjson)

set_target_properties(RaidNotes PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

# Precompiled header for faster compilation (uses full PCH since it depends on imgui)
target_precompile_headers(RaidNotes PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch.h>"
)

# RaidNotes depends on imgui, so it needs Vulkan include path for pch.h
find_package(Vulkan REQUIRED)
target_include_directories(RaidNotes PRIVATE ${Vulkan_INCLUDE_DIRS})
