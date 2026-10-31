find_package(ZLIB REQUIRED)

add_library(SnapshotManager STATIC
    src/SnapshotExporter.cpp
    src/SnapshotImporter.cpp
    src/StringTableBuilder.cpp
    src/StringTableReader.cpp
    src/ContextCollector.cpp
)

target_include_directories(SnapshotManager PUBLIC include)

target_link_libraries(SnapshotManager
    PUBLIC Database
    PUBLIC AnnotationManager
    PRIVATE ZLIB::ZLIB
)

set_target_properties(SnapshotManager PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

# Precompiled header for faster compilation (base PCH without Vulkan/ImGui)
target_precompile_headers(SnapshotManager PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch_base.h>"
)
