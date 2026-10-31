cmake_minimum_required(VERSION 3.12)
project(AwowLabTests LANGUAGES CXX)

# RapidJSON for the tests that include UnifiedSettings.
find_package(RapidJSON CONFIG REQUIRED)

# OpenSSL::Crypto, used by UnifiedSettings.
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)  # SpellNameDatabase reads spell_names.csv.gz

add_executable(tokenizer_tests
    tokenizer_tests.cpp
)

target_link_libraries(tokenizer_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
)

add_executable(parser_tests
    parser_tests.cpp
)

target_link_libraries(parser_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
)

add_executable(core_utility_tests
    core_utility_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/StringInterner.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/PhaseSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/PhaseResolver.cpp
)

target_link_libraries(core_utility_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
)

# PhaseSettings serializes through RapidJSON
target_include_directories(core_utility_tests
    PRIVATE
        ${RAPIDJSON_INCLUDE_DIRS}
)

add_executable(parser_unit_tests
    parser_unit_tests.cpp
    power_reallog_test.cpp
)

target_link_libraries(parser_unit_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
)

# The [reallog] case reads a combat log from the local WoW install. It warns
# and skips when the path is not there, so the suite still runs on a machine
# without the game installed.
set(AWOWLAB_REAL_LOG "" CACHE FILEPATH "Combat log used by the [reallog] tests")
target_compile_definitions(parser_unit_tests
    PRIVATE
        REAL_LOG_PATH="${AWOWLAB_REAL_LOG}"
)

add_executable(parser_integration_tests
    parser_integration_tests.cpp
)

target_link_libraries(parser_integration_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
)

# parser_integration_tests opens testevents.txt by relative path.
add_custom_command(TARGET parser_integration_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/testevents.txt"
        "$<TARGET_FILE_DIR:parser_integration_tests>/testevents.txt"
    COMMENT "Copying testevents.txt to build directory"
)

add_executable(position_test
    position_test.cpp
)

target_link_libraries(position_test
    PRIVATE
        awowlab_lib
)

add_executable(database_tests
    database_tests.cpp
    database_regression_tests.cpp
    annotation_regression_tests.cpp
    energize_reallog_test.cpp
    resource_csv_test.cpp
    package_csv_test.cpp
    package_builder_test.cpp
    rate_limit_backup_test.cpp
    spell_assignment_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/UIManager/src/UI/SpellAssignmentSettings.cpp
    settings_key_test.cpp
    haste_buff_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/SpellNameDatabase.cpp
    # The snapshot round trips and the trigger loader pull in AnnotationManager,
    # whose unity object includes UI code that reads localized strings.
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/LocalizationManager.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/StringInterner.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/MobWeightSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ColorUtils.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/UnifiedSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/Crypto.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/RegistryBackup.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/WclSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ExecutablePath.cpp
)

target_link_libraries(database_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
        OpenSSL::Crypto
        ZLIB::ZLIB  # SpellNameDatabase reads spell_names.csv.gz
        crypt32  # DPAPI wrapping of the WCL token (WclSettings.cpp)
)

target_compile_definitions(database_tests
    PRIVATE
        REAL_LOG_PATH="${AWOWLAB_REAL_LOG}"
        SPELL_NAMES_DIR="${CMAKE_SOURCE_DIR}/AwowLab/data/spellnames"
        TEST_EVENTS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/testevents.txt"
        HASTE_BUFFS_DIR="${CMAKE_SOURCE_DIR}/AwowLab/data"
)

target_include_directories(database_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab/Updater  # miniz.h, to read package zips back
)

target_include_directories(database_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab/Core
        ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils
        ${CMAKE_SOURCE_DIR}/AwowLab/UIManager/include  # SpellAssignmentSettings
        ${RAPIDJSON_INCLUDE_DIRS}
)

# Benchmark comparison against a mentor log. The truncation arithmetic and
# the cadence heuristics can be wrong while still producing plausible numbers.
add_executable(benchmark_tests
    benchmark_tests.cpp
    benchmark_engine_tests.cpp
    benchmark_perf_tests.cpp
    localization_format_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Benchmark/LivePullBridge.cpp
    # LivePullBridge pulls in the tokenizer and OutputWriter, which need these
    # Core utilities - they are not part of awowlab_lib.
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/StringInterner.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/MobWeightSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ColorUtils.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/UnifiedSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/Crypto.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/RegistryBackup.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/WclSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ExecutablePath.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Benchmark/MentorProfile.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Benchmark/SpellClassification.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Benchmark/ClassificationSuggester.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Benchmark/BenchmarkEngine.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Benchmark/ProfileExtractor.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/PhaseResolver.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Benchmark/BenchmarkStore.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/PhaseSettings.cpp
)

target_link_libraries(benchmark_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
        OpenSSL::Crypto
        crypt32
)

target_include_directories(benchmark_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab
        ${CMAKE_SOURCE_DIR}/AwowLab/Core
        ${CMAKE_SOURCE_DIR}/AwowLab/Parser/Include
        ${RAPIDJSON_INCLUDE_DIRS}
)

# The gap and downtime checks read each cast's haste
target_compile_definitions(benchmark_tests
    PRIVATE
        HASTE_BUFFS_DIR="${CMAKE_SOURCE_DIR}/AwowLab/data"
)

# The format tests read data/lang/*.csv.
add_custom_command(TARGET benchmark_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/AwowLab/data/lang"
        "$<TARGET_FILE_DIR:benchmark_tests>/data/lang"
    COMMENT "Copying language files for format checks"
)

# The perf cases re-parse a real log to time the live-pull path.
add_custom_command(TARGET benchmark_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/combat"
        "$<TARGET_FILE_DIR:benchmark_tests>/fixtures/combat"
    COMMENT "Copying combat fixtures for benchmark timings"
)

# The pet-attribution cases read a real log from fixtures/combat, which the
# tests open by relative path. Without this they fail on a missing file rather
# than on anything they actually assert.
add_custom_command(TARGET database_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/combat"
        "$<TARGET_FILE_DIR:database_tests>/fixtures/combat"
    COMMENT "Copying combat test fixtures to build directory"
)

# A DB2 column shift only shows up against the shipped spell_data_*.bin,
# which the fixture-based suites never load. Set AWOWLAB_E2E_LOG to a combat
# log to run these; without it they report as skipped.
add_executable(rollforward_e2e
    rollforward_e2e.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/StringInterner.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/MobWeightSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ColorUtils.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/UnifiedSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/Crypto.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/RegistryBackup.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/WclSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ExecutablePath.cpp
)

target_link_libraries(rollforward_e2e
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
        OpenSSL::Crypto
        crypt32
)

target_include_directories(rollforward_e2e
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab/Core
        ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils
        ${RAPIDJSON_INCLUDE_DIRS}
)

# Core utilities aren't part of awowlab_lib, so their sources are listed here.
add_executable(parity_tests
    parity/parity_tests.cpp
    parity/parity_test_helpers.cpp
    parity/wcl_resource_gear_test.cpp
    parity/wcl_actor_scope_test.cpp
    parity/wcl_baseline_live_test.cpp
    parity/wcl_dummy_zone_test.cpp
    parity/wcl_dummy_reset_test.cpp
    parity/wcl_rankings_args_test.cpp
    parity/wcl_single_fight_test.cpp
    parity/press_groups_test.cpp
    parity/wcl_damaged_cache_test.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/HeroTalents.cpp  # the baseline test resolves a build's tree from its talents
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/StringInterner.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/MobWeightSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ColorUtils.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/UnifiedSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/WarcraftLogsSession.cpp  # the live actor-scope test drives it
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/SpellNameDatabase.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/Crypto.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/RegistryBackup.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/WclSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ExecutablePath.cpp
)

target_link_libraries(parity_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
        warcraftlogs_manager
        OpenSSL::Crypto
        ZLIB::ZLIB  # SpellNameDatabase reads the gzipped names
        crypt32  # DPAPI wrapping of the WCL token (WclSettings.cpp)
)

# The cache-reading cases use the debug app's wcl_cache from this build tree.
# They warn and skip when it is missing, as it is on a fresh clone; point
# AWOWLAB_WCL_CACHE at another cache to run them anyway. The live cases write
# their packages under the build tree too.
set(AWOWLAB_WCL_CACHE "${CMAKE_BINARY_DIR}/AwowLab/Debug/wcl_cache"
    CACHE PATH "wcl_cache folder read by the parity tests")
target_compile_definitions(parity_tests
    PRIVATE
        WCL_CACHE_DIR="${AWOWLAB_WCL_CACHE}"
        PACKAGE_OUT_DIR="${CMAKE_BINARY_DIR}"
        SPELL_NAMES_DIR="${CMAKE_SOURCE_DIR}/AwowLab/data/spellnames"
        HERO_TALENTS_DIR="${CMAKE_SOURCE_DIR}/AwowLab/data"
        HASTE_BUFFS_DIR="${CMAKE_SOURCE_DIR}/AwowLab/data"
)

target_include_directories(parity_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/parity
        ${CMAKE_SOURCE_DIR}/AwowLab/Updater  # miniz.h, to read package zips
        ${CMAKE_SOURCE_DIR}/AwowLab/warcraftlogs_manager/include
        ${CMAKE_SOURCE_DIR}/AwowLab/Core
        ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils
        ${RAPIDJSON_INCLUDE_DIRS}
)

add_custom_command(TARGET parity_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parity"
        "$<TARGET_FILE_DIR:parity_tests>/fixtures/parity"
    COMMENT "Copying parity test fixtures to build directory"
)

# Built for debug configurations only.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    add_executable(fixture_generator
        parity/fixture_generator/main.cpp
        parity/fixture_generator/fixture_generator.cpp
        ${CMAKE_SOURCE_DIR}/AwowLab/Core/StringInterner.cpp
        ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ColorUtils.cpp
    )

    target_link_libraries(fixture_generator
        PRIVATE
            awowlab_lib
            warcraftlogs_manager
    )

    target_include_directories(fixture_generator
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/parity/fixture_generator
            ${CMAKE_SOURCE_DIR}/AwowLab/warcraftlogs_manager/include
            ${CMAKE_SOURCE_DIR}/AwowLab/Core
            ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils
    )
endif()

add_executable(livelog_scan_tests
    livelog_scan_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Overlay/LiveLogSession.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Overlay/LiveCombatStats.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Overlay/ActiveLogWatcher.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/StringInterner.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/MobWeightSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ColorUtils.cpp
    # LiveCombatStats drives the stat databases, whose spell-GCD loader
    # reaches SettingsCache; pull in the settings sources the same way
    # database_tests does.
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/UnifiedSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/Crypto.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/RegistryBackup.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Settings/WclSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ExecutablePath.cpp
)

target_link_libraries(livelog_scan_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
        OpenSSL::Crypto
        crypt32  # DPAPI wrapping of the WCL token (WclSettings.cpp)
)

target_include_directories(livelog_scan_tests
    PRIVATE
        ${RAPIDJSON_INCLUDE_DIRS}
)

target_include_directories(livelog_scan_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab
        ${CMAKE_SOURCE_DIR}/AwowLab/Core
        ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils
)

add_executable(updater_tests
    updater_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/SignatureVerifier.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/ManifestParser.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/ReleaseChecks.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/PathValidator.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/HttpDownloader.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/FileReplacer.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/HashVerifier.cpp
)

target_link_libraries(updater_tests
    PRIVATE
        Catch2::Catch2WithMain
        rapidjson
        awowlab_build_options  # Release signing key (awowlab.config.json)
        bcrypt
        crypt32
        winhttp
)

target_include_directories(updater_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab/Updater
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(TARGET updater_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/updater"
        "$<TARGET_FILE_DIR:updater_tests>/fixtures/updater"
    COMMENT "Copying updater test fixtures to build directory"
)

# Standalone: LocalizationManager only needs the standard library, so no
# awowlab_lib link (keeps the test build light)
add_executable(localization_tests
    localization_tests.cpp
    localization_specifier_tests.cpp
    shipped_csv_test.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/LocalizationManager.cpp
)

target_link_libraries(localization_tests
    PRIVATE
        Catch2::Catch2WithMain
)

target_include_directories(localization_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab
        ${CMAKE_SOURCE_DIR}/AwowLab/Core
)

# Point the shipped-CSV test at the real language files in the source tree
target_compile_definitions(localization_tests
    PRIVATE
        LANG_DIR="${CMAKE_SOURCE_DIR}/AwowLab/data/lang"
)

# ColorUtils isn't part of awowlab_lib, so its source is listed here.
add_executable(color_utils_tests
    color_utils_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils/ColorUtils.cpp
)

target_link_libraries(color_utils_tests
    PRIVATE
        Catch2::Catch2WithMain
        awowlab_lib
)

target_include_directories(color_utils_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab/Core
        ${CMAKE_SOURCE_DIR}/AwowLab/Core/Utils
)

add_executable(oauth_callback_tests
    oauth_callback_tests.cpp
)

target_link_libraries(oauth_callback_tests
    PRIVATE
        Catch2::Catch2WithMain
        warcraftlogs_http
)

target_compile_definitions(oauth_callback_tests
    PRIVATE
        _WIN32_WINNT=0x0A00
)

# Replay-view logic that needs no GPU. Sources are pulled in one by one so
# the suite doesn't link the Vulkan renderer.
add_executable(render_logic_tests
    render_logic_tests.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/UIManager/src/Playback/PlaybackController.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/UIManager/src/PlayAlong/PlayAlongKeybindSettings.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/UIManager/src/Geometry/BakedModelPack.cpp
)

target_link_libraries(render_logic_tests
    PRIVATE
        Catch2::Catch2WithMain
)

target_include_directories(render_logic_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab/UIManager/include
        ${RAPIDJSON_INCLUDE_DIRS}
        # Key constants only; nothing here calls into ImGui
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)

# UIManager builds these sources with its precompiled header, which keeps
# the standard library quiet; without it the spec name table trips C4244
# inside <utility>
if (MSVC)
    target_compile_options(render_logic_tests PRIVATE /external:anglebrackets /external:W0)
endif()

# Register the suites with CTest, so `ctest --test-dir <build> -C <config>` runs
# them all. Each runs from its own output folder: the fixtures are copied there
# and opened by relative path, so run from anywhere else they are not found.
# Suites that need local data or live WarcraftLogs access skip themselves.
foreach(suite
        tokenizer_tests parser_tests core_utility_tests parser_unit_tests
        parser_integration_tests position_test database_tests benchmark_tests
        rollforward_e2e parity_tests livelog_scan_tests updater_tests
        localization_tests color_utils_tests oauth_callback_tests
        render_logic_tests)
    add_test(NAME ${suite} COMMAND ${suite} WORKING_DIRECTORY $<TARGET_FILE_DIR:${suite}>)
endforeach()
