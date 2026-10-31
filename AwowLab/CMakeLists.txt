if(POLICY CMP0079)
  cmake_policy(SET CMP0079 NEW)
endif()

# ---------------------------------------------------------------------------
# Build options (see docs/BUILD-OPTIONS.md)
#
# The updater is off unless asked for. A build made from source that updates
# itself would be replaced by the official binary, silently undoing whatever
# was built. The x64-release preset, which the official releases are built
# from, turns it on.
# ---------------------------------------------------------------------------
option(AWOWLAB_ENABLE_UPDATER
    "Build the updater and let the app update itself from AWOWLAB_UPDATE_REPO" OFF)

# Repositories, the WarcraftLogs client, the release signing key and the
# settings file key come from awowlab.config.json at the repository root.
# Every builder has their own copy and it is never committed: the first
# configure makes it from awowlab.config.example.json. Its values are the
# defaults of the cache variables below, so -D still overrides any of them. A
# cache value nobody overrode follows later edits to the file; one set with
# -D stays until it is set again or removed from the cache.
cmake_path(SET _awowlab_default_config NORMALIZE "${CMAKE_SOURCE_DIR}/awowlab.config.json")
set(AWOWLAB_CONFIG_FILE "${_awowlab_default_config}" CACHE FILEPATH
    "JSON file holding this build's repositories, WarcraftLogs client id and keys")
if(NOT EXISTS "${AWOWLAB_CONFIG_FILE}")
    cmake_path(COMPARE "${AWOWLAB_CONFIG_FILE}" EQUAL "${_awowlab_default_config}" _awowlab_is_default)
    if(NOT _awowlab_is_default)
        message(FATAL_ERROR "AWOWLAB_CONFIG_FILE not found: ${AWOWLAB_CONFIG_FILE}")
    endif()
    file(READ "${CMAKE_SOURCE_DIR}/awowlab.config.example.json" _awowlab_config)
    set(_awowlab_config_created TRUE)
else()
    file(READ "${AWOWLAB_CONFIG_FILE}" _awowlab_config)
    set(_awowlab_config_created FALSE)
endif()

# A config made before a field existed does not have it. Copy the example's
# value in, so the builder's file always lists every value a fork can set.
if(NOT _awowlab_config_created)
    file(READ "${CMAKE_SOURCE_DIR}/awowlab.config.example.json" _awowlab_example)
    string(JSON _example_count LENGTH "${_awowlab_example}")
    math(EXPR _example_last "${_example_count} - 1")
    set(_awowlab_added "")
    foreach(_index RANGE ${_example_last})
        string(JSON _name MEMBER "${_awowlab_example}" ${_index})
        string(JSON _present ERROR_VARIABLE _absent GET "${_awowlab_config}" "${_name}")
        if(NOT _absent OR _name MATCHES "^_")
            continue()
        endif()
        string(JSON _type TYPE "${_awowlab_example}" "${_name}")
        string(JSON _value GET "${_awowlab_example}" "${_name}")
        if(_type STREQUAL "STRING")
            string(REPLACE "\\" "\\\\" _value "${_value}")
            string(REPLACE "\"" "\\\"" _value "${_value}")
            set(_value "\"${_value}\"")
        endif()
        string(JSON _awowlab_config SET "${_awowlab_config}" "${_name}" "${_value}")
        list(APPEND _awowlab_added "${_name}")
    endforeach()
    if(_awowlab_added)
        file(WRITE "${AWOWLAB_CONFIG_FILE}" "${_awowlab_config}\n")
        list(JOIN _awowlab_added ", " _awowlab_added)
        message(NOTICE "Added ${_awowlab_added} to ${AWOWLAB_CONFIG_FILE} with the values from "
            "awowlab.config.example.json; change them there if your build needs others.")
    endif()
endif()

# The settings file is encrypted with a key derived from settings_key. A
# builder who has none gets a random one, kept in the config so later
# configures reuse it and the settings survive rebuilds. It only has to
# differ from other builds, so CMake's own generator is good enough. Releases
# must use the release key instead, or users lose their settings on update;
# settings_key_generated tells tools/release/release.py to refuse.
string(JSON _settings_key ERROR_VARIABLE _settings_key_error GET "${_awowlab_config}" settings_key)
if(_settings_key_error OR _settings_key STREQUAL "")
    string(RANDOM LENGTH 64 ALPHABET "0123456789abcdef" _settings_key)
    string(JSON _awowlab_config SET "${_awowlab_config}" settings_key "\"${_settings_key}\"")
    string(JSON _awowlab_config SET "${_awowlab_config}" settings_key_generated true)
    file(WRITE "${AWOWLAB_CONFIG_FILE}" "${_awowlab_config}\n")
    if(_awowlab_config_created)
        message(NOTICE "Created ${AWOWLAB_CONFIG_FILE} from awowlab.config.example.json, with a "
            "settings key generated for this checkout. Fill in your own WarcraftLogs client id, "
            "repositories and signing key there before publishing anything; never commit it.")
    else()
        message(NOTICE "Generated a settings key for this checkout in ${AWOWLAB_CONFIG_FILE}. "
            "Fine for your own builds; releases need the release key there instead.")
    endif()
elseif(_awowlab_config_created)
    file(WRITE "${AWOWLAB_CONFIG_FILE}" "${_awowlab_config}")
    message(NOTICE "Created ${AWOWLAB_CONFIG_FILE} from awowlab.config.example.json; never commit it.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AWOWLAB_CONFIG_FILE}")

function(awowlab_config_option var key type doc)
    string(JSON _value ERROR_VARIABLE _error GET "${_awowlab_config}" "${key}")
    if(_error)
        message(FATAL_ERROR "${AWOWLAB_CONFIG_FILE}: cannot read \"${key}\": ${_error}")
    endif()
    # A list becomes its items separated by ", ", ready for a C initializer
    string(JSON _type TYPE "${_awowlab_config}" "${key}")
    if(_type STREQUAL "ARRAY")
        string(JSON _count LENGTH "${_awowlab_config}" "${key}")
        set(_items "")
        if(_count GREATER 0)
            math(EXPR _last "${_count} - 1")
            foreach(_index RANGE ${_last})
                string(JSON _item GET "${_awowlab_config}" "${key}" ${_index})
                list(APPEND _items "${_item}")
            endforeach()
        endif()
        list(JOIN _items ", " _value)
    endif()
    # Remember what the file said last time, so an untouched cache entry can
    # tell itself apart from a -D override and pick up the new value.
    if(NOT DEFINED CACHE{${var}} OR "${${var}}" STREQUAL "${_AWOWLAB_CONFIG_${var}}")
        set(${var} "${_value}" CACHE ${type} "${doc}" FORCE)
    endif()
    set(_AWOWLAB_CONFIG_${var} "${_value}" CACHE INTERNAL "")
    set(AWOWLAB_CONFIG_${var} "${_value}" PARENT_SCOPE)
endfunction()

awowlab_config_option(AWOWLAB_PROJECT_URL project_url STRING
    "Web page of the project; the landing page links its issues and releases")
awowlab_config_option(AWOWLAB_UPDATE_REPO update_repo STRING
    "GitHub repository (owner/name) whose releases the updater installs")
awowlab_config_option(AWOWLAB_ASSETS_REPO assets_repo STRING
    "GitHub repository (owner/name) holding the static assets release")
awowlab_config_option(AWOWLAB_WCL_CLIENT_ID wcl_client_id STRING
    "WarcraftLogs OAuth client id used for sign-in")
awowlab_config_option(AWOWLAB_WCL_REDIRECT_PORTS wcl_redirect_ports STRING
    "Local ports sign-in listens on, in order; each is a redirect URL on the WarcraftLogs client")
awowlab_config_option(AWOWLAB_SIGNING_PUBLIC_KEY signing_public_key STRING
    "Release signing public key, ECDSA P-256 X then Y as 128 hex characters")
awowlab_config_option(AWOWLAB_SETTINGS_KEY settings_key STRING
    "Key material the settings file encryption key is derived from")

if(NOT AWOWLAB_PROJECT_URL MATCHES "^https://[A-Za-z0-9._~/%-]+[^/]$")
    message(FATAL_ERROR "AWOWLAB_PROJECT_URL must be an https:// address with no trailing slash, got '${AWOWLAB_PROJECT_URL}'")
endif()

if(NOT AWOWLAB_UPDATE_REPO MATCHES "^([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)$")
    message(FATAL_ERROR "AWOWLAB_UPDATE_REPO must look like owner/name, got '${AWOWLAB_UPDATE_REPO}'")
endif()
set(AWOWLAB_UPDATE_REPO_OWNER "${CMAKE_MATCH_1}")
set(AWOWLAB_UPDATE_REPO_NAME "${CMAKE_MATCH_2}")

if(NOT AWOWLAB_ASSETS_REPO MATCHES "^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "AWOWLAB_ASSETS_REPO must look like owner/name, got '${AWOWLAB_ASSETS_REPO}'")
endif()

if(NOT AWOWLAB_WCL_CLIENT_ID MATCHES "^[A-Za-z0-9-]+$")
    message(FATAL_ERROR "AWOWLAB_WCL_CLIENT_ID must be a WarcraftLogs client id, got '${AWOWLAB_WCL_CLIENT_ID}'")
endif()

# Becomes a C initializer list
if(NOT AWOWLAB_WCL_REDIRECT_PORTS MATCHES "^[0-9]+(, *[0-9]+)*$")
    message(FATAL_ERROR "AWOWLAB_WCL_REDIRECT_PORTS must be one or more port numbers, got '${AWOWLAB_WCL_REDIRECT_PORTS}'")
endif()
string(REPLACE "," ";" _ports "${AWOWLAB_WCL_REDIRECT_PORTS}")
foreach(_port IN LISTS _ports)
    string(STRIP "${_port}" _port)
    if(_port LESS 1 OR _port GREATER 65535)
        message(FATAL_ERROR "AWOWLAB_WCL_REDIRECT_PORTS: ${_port} is not a port number")
    endif()
endforeach()

# Becomes a C string literal, so no quotes, backslashes or spaces
if(NOT AWOWLAB_SETTINGS_KEY MATCHES "^[A-Za-z0-9_.:+/=-]+$")
    message(FATAL_ERROR "AWOWLAB_SETTINGS_KEY must be letters, digits or _.:+/=- and not empty")
endif()

# The key becomes a C initializer list. An empty key means releases are not
# checked at all, which only a build without the updater can live with.
if(AWOWLAB_SIGNING_PUBLIC_KEY STREQUAL "")
    if(AWOWLAB_ENABLE_UPDATER)
        message(FATAL_ERROR "AWOWLAB_ENABLE_UPDATER needs a signing key: set signing_public_key "
            "in ${AWOWLAB_CONFIG_FILE} (see tools/release/SIGNING.md)")
    endif()
    set(AWOWLAB_SIGNING_KEY_CONFIGURED 0)
    string(REPEAT "0x00, " 64 AWOWLAB_SIGNING_PUBLIC_KEY_BYTES)
else()
    if(NOT AWOWLAB_SIGNING_PUBLIC_KEY MATCHES "^[0-9A-Fa-f]+$")
        message(FATAL_ERROR "AWOWLAB_SIGNING_PUBLIC_KEY must be hex, got '${AWOWLAB_SIGNING_PUBLIC_KEY}'")
    endif()
    string(LENGTH "${AWOWLAB_SIGNING_PUBLIC_KEY}" _key_length)
    if(NOT _key_length EQUAL 128)
        message(FATAL_ERROR "AWOWLAB_SIGNING_PUBLIC_KEY must be 128 hex characters (64 bytes), got ${_key_length}")
    endif()
    set(AWOWLAB_SIGNING_KEY_CONFIGURED 1)
    string(TOUPPER "${AWOWLAB_SIGNING_PUBLIC_KEY}" _key_hex)
    set(AWOWLAB_SIGNING_PUBLIC_KEY_BYTES "")
    foreach(_offset RANGE 0 126 2)
        string(SUBSTRING "${_key_hex}" ${_offset} 2 _byte)
        string(APPEND AWOWLAB_SIGNING_PUBLIC_KEY_BYTES "0x${_byte}, ")
    endforeach()
endif()
string(STRIP "${AWOWLAB_SIGNING_PUBLIC_KEY_BYTES}" AWOWLAB_SIGNING_PUBLIC_KEY_BYTES)

message(STATUS "AwowLab config: ${AWOWLAB_CONFIG_FILE}")
message(STATUS "AwowLab updater: ${AWOWLAB_ENABLE_UPDATER} (${AWOWLAB_UPDATE_REPO})")
foreach(_overridable AWOWLAB_PROJECT_URL AWOWLAB_UPDATE_REPO AWOWLAB_ASSETS_REPO
                     AWOWLAB_WCL_CLIENT_ID AWOWLAB_WCL_REDIRECT_PORTS AWOWLAB_SIGNING_PUBLIC_KEY)
    if(NOT "${${_overridable}}" STREQUAL "${AWOWLAB_CONFIG_${_overridable}}")
        message(STATUS "AwowLab ${_overridable} overridden: ${${_overridable}}")
    endif()
endforeach()
if(NOT AWOWLAB_SETTINGS_KEY STREQUAL AWOWLAB_CONFIG_AWOWLAB_SETTINGS_KEY)
    message(STATUS "AwowLab AWOWLAB_SETTINGS_KEY overridden")
endif()

configure_file(Core/BuildOptions.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/AwowLabBuildOptions.h" @ONLY)
add_library(awowlab_build_options INTERFACE)
target_include_directories(awowlab_build_options INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Create an INTERFACE library that groups your modules
add_library(awowlab_lib INTERFACE)

# Add your modules subdirectories (make sure they define their own static/shared libs)
add_subdirectory("AssetManager")           # Asset path resolution with user/defaults layering
add_subdirectory("MMapManager")
add_subdirectory("Txttokenizer")
add_subdirectory("Parser")
add_subdirectory("Database")
add_subdirectory("UIManager")
add_subdirectory("warcraftlogs_http")      # HTTP client with Boost.Beast (isolated for fast rebuilds)
add_subdirectory("warcraftlogs_manager")   # WarcraftLogs API client
add_subdirectory("AnnotationManager")      # Annotation system for visualizing mechanics
add_subdirectory("RaidNotes")              # ERT-compatible raid notes creation and export
add_subdirectory("SnapshotManager")        # Binary snapshot export/import for sharing replay clips
if(AWOWLAB_ENABLE_UPDATER)
    add_subdirectory("Updater")            # Auto-updater executable (separate minimal binary)
endif()

# Link the modules as INTERFACE dependencies
# Note: Link order matters on GCC! Libraries that are depended upon must come AFTER the libraries that depend on them.
# UIManager depends on AnnotationManager, so AnnotationManager must come after UIManager
target_link_libraries(awowlab_lib INTERFACE
    MMapManager
    Txttokenizer
    Parser
    Database
    UIManager
    AnnotationManager
    RaidNotes
    SnapshotManager
    warcraftlogs_http
    warcraftlogs_manager
    AssetManager
)

# Share precompiled header with module libraries for faster compilation
target_precompile_headers(awowlab_lib INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)

# Find OpenSSL for encrypted settings
find_package(OpenSSL REQUIRED)

# Find zlib for gzip decompression (spell names database)
find_package(ZLIB REQUIRED)

# RapidJSON for UnifiedSettings JSON serialization
find_package(RapidJSON CONFIG REQUIRED)

# Define your main executable
# Use WIN32 in Release builds to hide console window
add_executable(AwowLab WIN32
    AwowLab.cpp
    AwowLab.h
    Core/Application.cpp
    Core/Application.h
    Core/FileDialog.h
    Core/LogSession.cpp
    Core/LogSession.h
    Core/PerfMonitor.cpp
    Core/PerfMonitor.h
    Core/FrameProfiler.h
    Core/StringInterner.cpp
    Core/StringInterner.h
    Core/MobWeightSettings.cpp
    Core/MobWeightSettings.h
    Core/PhaseSettings.cpp
    Core/PhaseSettings.h
    Core/PhaseResolver.cpp
    Core/PhaseResolver.h

    # Benchmark - compare a pull against a reference log the user picked.
    # Main app only; the overlay has no encounter to compare against.
    Core/Benchmark/BenchmarkStore.cpp
    Core/Benchmark/BenchmarkStore.h
    Core/Benchmark/MentorProfile.cpp
    Core/Benchmark/MentorProfile.h
    Core/Benchmark/SpellClassification.cpp
    Core/Benchmark/SpellClassification.h
    Core/Benchmark/ClassificationSuggester.cpp
    Core/Benchmark/ClassificationSuggester.h
    Core/Benchmark/ProfileExtractor.cpp
    Core/Benchmark/ProfileExtractor.h
    Core/Benchmark/BenchmarkEngine.cpp
    Core/Benchmark/BenchmarkEngine.h
    Core/Benchmark/BenchmarkSession.cpp
    Core/Benchmark/BenchmarkSession.h
    Core/Benchmark/LivePullBridge.cpp
    Core/Benchmark/LivePullBridge.h

    Core/UnifiedSettings.cpp
    Core/UnifiedSettings.h
    Core/Settings/Crypto.cpp
    Core/Settings/WclSettings.cpp
    Core/Settings/WclSettings.h
    Core/Settings/SettingsExportManager.cpp
    Core/Settings/SettingsExportManager.h
    Core/Settings/IExportableSettings.h
    Core/Settings/ExportableSettingsAdapters.cpp
    Core/Settings/ExportableSettingsAdapters.h
    Core/WarcraftLogsSession.cpp
    Core/WarcraftLogsSession.h
    Core/LocalizationManager.cpp
    Core/LocalizationManager.h
    Core/SpellNameDatabase.cpp
    Core/HeroTalents.cpp
    Core/SpellNameDatabase.h
    Core/Utils/ExecutablePath.cpp
    Core/Utils/ExecutablePath.h
    Core/Utils/ColorUtils.cpp
    Core/Utils/ColorUtils.h
    Core/EncounterDataLoader.cpp
    Core/EncounterDataLoader.h
    Core/FileAssociation.cpp
    Core/FileAssociation.h
    Core/SingleInstance.cpp
    Core/SingleInstance.h
    # Flow classes - split from Application.cpp
    # Overlay module
    Core/Overlay/ActiveLogWatcher.cpp
    Core/Overlay/ActiveLogWatcher.h
    Core/Overlay/LiveLogSession.cpp
    Core/Overlay/LiveLogSession.h
    Core/Overlay/LiveLogManager.cpp
    Core/Overlay/LiveLogManager.h
    Core/Overlay/LiveCombatStats.cpp
    Core/Overlay/LiveCombatStats.h
    Core/Overlay/PullSegment.h
    Core/Overlay/PullEndHook.cpp
    Core/Overlay/PullEndHook.h
    Core/Overlay/OverlayApplication.cpp
    Core/Overlay/OverlayApplication.h
    Core/Overlay/OverlayVulkanContext.cpp
    Core/Overlay/OverlayVulkanContext.h
    $<$<PLATFORM_ID:Windows>:AwowLab.rc>
)

# Precompiled header for faster compilation
target_precompile_headers(AwowLab PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)

# In Debug builds, show console window for debug output
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    set_target_properties(AwowLab PROPERTIES
        WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
    )
endif()

# Link your executable against the interface library (so it inherits includes, etc.)
# crypt32 backs the DPAPI wrapping of the WCL token (Settings/WclSettings.cpp)
target_link_libraries(AwowLab PRIVATE awowlab_lib OpenSSL::Crypto ZLIB::ZLIB crypt32)

# Add portable-file-dialogs include directory
target_include_directories(AwowLab PRIVATE
    ${portable-file-dialogs_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/UIManager/include
)

# Copy shaders to AwowLab executable directory
add_custom_command(TARGET AwowLab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:AwowLab>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/UIManager/shaders
        $<TARGET_FILE_DIR:AwowLab>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/AnnotationManager/shaders
        $<TARGET_FILE_DIR:AwowLab>/shaders
    COMMENT "Copying shaders to AwowLab executable directory"
)

# Create symlink/junction to PNG icons folder (much faster than copying 200MB+ each build)
# This folder is hosted separately and not in git, so it may not exist in all environments
# IMPORTANT: This must run BEFORE creating subdirectories, otherwise PNG becomes a regular folder
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PNG")
    if(WIN32)
        # Use PowerShell to create junction on Windows - remove existing non-junction folder if needed
        add_custom_command(TARGET AwowLab POST_BUILD
            COMMAND powershell -Command "$pngPath = '$<TARGET_FILE_DIR:AwowLab>/PNG'; if (Test-Path $pngPath) { $item = Get-Item $pngPath; if ($item.LinkType -ne 'Junction') { Remove-Item -Recurse -Force $pngPath; New-Item -ItemType Junction -Path $pngPath -Target '${CMAKE_CURRENT_SOURCE_DIR}/PNG' | Out-Null; Write-Host 'Replaced folder with PNG junction' } else { Write-Host 'PNG junction already exists' } } else { New-Item -ItemType Junction -Path $pngPath -Target '${CMAKE_CURRENT_SOURCE_DIR}/PNG' | Out-Null; Write-Host 'Created PNG junction' }"
            COMMENT "Linking spell icon PNG folder to AwowLab executable directory"
        )
    else()
        # Use ln -sf on Linux/Unix
        add_custom_command(TARGET AwowLab POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:AwowLab>/PNG"
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_CURRENT_SOURCE_DIR}/PNG" "$<TARGET_FILE_DIR:AwowLab>/PNG"
            COMMENT "Creating PNG symlink to spell icon folder"
        )
    endif()
else()
    # No source PNG folder - create directories only
    message(STATUS "PNG folder not found at ${CMAKE_CURRENT_SOURCE_DIR}/PNG - icons will not be copied (this is expected if icons are hosted separately)")
    add_custom_command(TARGET AwowLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AwowLab>/PNG/Spell Icons"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AwowLab>/PNG/Worldmaps"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/PNG/Worldmaps/map_world_bounds.csv"
            "$<TARGET_FILE_DIR:AwowLab>/PNG/Worldmaps/map_world_bounds.csv"
        COMMENT "Creating PNG folders (no source PNG folder)"
    )
endif()

# Copy data folder (spell GCD database, etc.) to output directory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_custom_command(TARGET AwowLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AwowLab>/data"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:AwowLab>/data"
        COMMENT "Copying data folder to AwowLab executable directory"
    )
endif()

# Copy application icon to output directory (for GLFW window icon at runtime)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AwowLab_icon.png")
    add_custom_command(TARGET AwowLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/AwowLab_icon.png"
            "$<TARGET_FILE_DIR:AwowLab>/AwowLab_icon.png"
        COMMENT "Copying application icon to AwowLab executable directory"
    )
endif()

# Copy updater executable to updater/ subdirectory after main app builds
# We attach this to AwowLab target and add dependency to ensure updater is built first
if(AWOWLAB_ENABLE_UPDATER)
    add_dependencies(AwowLab AwowLabUpdater)
    add_custom_command(TARGET AwowLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AwowLab>/updater"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:AwowLabUpdater>"
            "$<TARGET_FILE_DIR:AwowLab>/updater/AwowLabUpdater.exe"
        COMMENT "Copying AwowLabUpdater to updater subdirectory"
    )
endif()

# ---------------------------------------------------------------------------
# Standalone live overlay executable
#
# Links only the overlay's slice of the codebase (Core/Overlay, the shared
# settings/localization files below, UIMeterCore, Database, Parser,
# Txttokenizer). This source list is the boundary for what the overlay
# needs, so keep it explicit and alphabetized.
# ---------------------------------------------------------------------------

# The overlay ships as one self-contained executable: the language CSVs and
# the UI font are turned into byte arrays at build time and compiled in
# (declarations in Core/Overlay/EmbeddedOverlayAssets.h). Adding a new lang
# CSV requires a reconfigure so the GLOB picks it up.
set(OVERLAY_EMBED_CPP "${CMAKE_CURRENT_BINARY_DIR}/overlay_embedded/EmbeddedOverlayAssets.cpp")
file(GLOB OVERLAY_LANG_CSVS "${CMAKE_CURRENT_SOURCE_DIR}/data/lang/*.csv")
add_custom_command(
    OUTPUT "${OVERLAY_EMBED_CPP}"
    COMMAND ${CMAKE_COMMAND}
        "-DDATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data"
        "-DOUTPUT_CPP=${OVERLAY_EMBED_CPP}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_overlay_assets.cmake"
    DEPENDS ${OVERLAY_LANG_CSVS}
        "${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/NotoSans-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_overlay_assets.cmake"
    COMMENT "Embedding language files and UI font into AwowLabOverlay"
)

add_executable(AwowLabOverlay WIN32
    Core/LocalizationManager.cpp
    Core/LocalizationManager.h
    Core/MobWeightSettings.cpp
    Core/MobWeightSettings.h
    Core/Overlay/ActiveLogWatcher.cpp
    Core/Overlay/ActiveLogWatcher.h
    Core/Overlay/EmbeddedOverlayAssets.h
    Core/Overlay/LiveCombatStats.cpp
    Core/Overlay/LiveCombatStats.h
    Core/Overlay/LiveLogManager.cpp
    Core/Overlay/LiveLogManager.h
    Core/Overlay/LiveLogSession.cpp
    Core/Overlay/LiveLogSession.h
    Core/Overlay/OverlayApplication.cpp
    Core/Overlay/OverlayApplication.h
    Core/Overlay/OverlayMain.cpp
    Core/Overlay/OverlayVulkanContext.cpp
    Core/Overlay/OverlayVulkanContext.h
    Core/Overlay/PullSegment.h
    Core/Overlay/PullEndHook.cpp
    Core/Overlay/PullEndHook.h
    Core/PhaseSettings.cpp
    Core/PhaseSettings.h
    Core/PhaseResolver.cpp
    Core/PhaseResolver.h
    Core/Settings/Crypto.cpp
    Core/SpellNameDatabase.cpp
    Core/HeroTalents.cpp
    Core/SpellNameDatabase.h
    Core/StringInterner.cpp
    Core/StringInterner.h
    Core/UnifiedSettings.cpp
    Core/UnifiedSettings.h
    Core/Utils/ColorUtils.cpp
    Core/Utils/ColorUtils.h
    Core/Utils/ExecutablePath.cpp
    Core/Utils/ExecutablePath.h
    "${OVERLAY_EMBED_CPP}"
)

target_precompile_headers(AwowLabOverlay PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.h>"
)

# Same Debug-console arrangement as the main app
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    set_target_properties(AwowLabOverlay PROPERTIES
        WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
    )
endif()

# UIMeterCore pulls in imgui/glfw/Vulkan/Parser/Database transitively.
# OpenSSL backs the encrypted settings (Settings/Crypto.cpp) and zlib the
# gzip-compressed spell name files.
target_link_libraries(AwowLabOverlay PRIVATE
    UIMeterCore
    Database
    Parser
    Txttokenizer
    OpenSSL::Crypto
    ZLIB::ZLIB
    awowlab_build_options  # Settings file key (Settings/Crypto.cpp)
)

# No data copy: the overlay's language files and font are compiled into
# the executable (see the embed step above), so it runs standalone.
# No PNG folder: the overlay never creates a SpellIconLoader, so spell
# icons don't render there. No shader copy either - the overlay renders
# through the ImGui Vulkan backend, which carries its own embedded shaders.

