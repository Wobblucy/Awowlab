cmake_minimum_required(VERSION 3.20)
project(AwowLabUpdater LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(RapidJSON CONFIG REQUIRED)

set(UPDATER_SOURCES
    main.cpp
    Updater.cpp
    HttpDownloader.cpp
    HashVerifier.cpp
    FileReplacer.cpp
    ProgressWindow.cpp
    ManifestParser.cpp
    SignatureVerifier.cpp
    ReleaseChecks.cpp
    ZipExtractor.cpp
    PathValidator.cpp
)

# miniz.c is third-party; build it without our warning level.
set_source_files_properties(miniz.c PROPERTIES COMPILE_FLAGS "/W0")

set(UPDATER_HEADERS
    Updater.h
    HttpDownloader.h
    HashVerifier.h
    FileReplacer.h
    ProgressWindow.h
    ManifestParser.h
    SignatureVerifier.h
    ReleaseChecks.h
    UpdateSigningKey.h
    ZipExtractor.h
    PathValidator.h
    miniz.h
)

# WIN32: no console window.
add_executable(AwowLabUpdater WIN32
    ${UPDATER_SOURCES}
    ${UPDATER_HEADERS}
    miniz.c
)

target_include_directories(AwowLabUpdater PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/AwowLab/Core  # For Version.h
)

target_link_libraries(AwowLabUpdater PRIVATE
    winhttp      # Windows HTTP client
    bcrypt       # Windows Crypto API for SHA256
    comctl32     # Common controls (progress bar)
    rapidjson    # JSON parsing
    awowlab_build_options  # Release repository and signing key (awowlab.config.json)
)

# Release builds favour size.
if(MSVC)
    target_compile_options(AwowLabUpdater PRIVATE
        $<$<CONFIG:Release>:/O1>
        $<$<CONFIG:Release>:/Os>
        /W4
    )
    target_link_options(AwowLabUpdater PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>   # Remove unreferenced functions
        $<$<CONFIG:Release>:/OPT:ICF>   # Identical COMDAT folding
    )
endif()

set_target_properties(AwowLabUpdater PROPERTIES
    OUTPUT_NAME "AwowLabUpdater"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/AwowLab/Debug/updater"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/AwowLab/Release/updater"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/AwowLab/RelWithDebInfo/updater"
)
