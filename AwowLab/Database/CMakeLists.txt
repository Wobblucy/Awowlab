cmake_minimum_required(VERSION 3.12)
project(Database LANGUAGES CXX)

# RapidJSON for JSON parsing (via vcpkg)
find_package(RapidJSON CONFIG REQUIRED)

# Build Database as a static library
# miniz.c is vendored third-party, shared with the updater - no warnings
set_source_files_properties(${CMAKE_SOURCE_DIR}/AwowLab/Updater/miniz.c
                            PROPERTIES COMPILE_FLAGS "/W0")

add_library(Database STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputWriter/ActorMapBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OutputWriter/CSVExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CombatDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CombatDatabase/Aggregators.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CombatDatabase/RankingQueries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CombatDatabase/DamageTakenQueries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CombatDatabase/TimeSeriesQueries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AuraDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DeathDatabase_Positions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DispelDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AvoidanceDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AbsorbDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DebugDatabaseDumper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AnalysisPackage/ResourceCsvWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AnalysisPackage/PackageCsvWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AnalysisPackage/PackageBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AnalysisPackage/PressGroups.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AnalysisPackage/Expectation.cpp
    ${CMAKE_SOURCE_DIR}/AwowLab/Updater/miniz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/TalentDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpellDataTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CooldownCalculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CooldownModifiers/CooldownModifierRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HasteBuffData.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CooldownReductionTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpellCastDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ChargeManager.cpp
)

target_include_directories(Database
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # For Core/StringInterner.h access
        ${CMAKE_CURRENT_SOURCE_DIR}/../AnnotationManager/include  # For AutoAnnotationTypes.h access in debug builds
        ${RAPIDJSON_INCLUDE_DIRS}  # For CooldownModifierRegistry JSON loading
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AwowLab/Updater  # miniz.h, for zipping analysis packages
        ${CMAKE_SOURCE_DIR}/AwowLab/warcraftlogs_manager/include  # RankingSample for baselines
)

# Database depends on Parser for SpellDamageEvent, SwingDamageEvent, etc.
target_link_libraries(Database PUBLIC Parser)

# Precompiled header for faster compilation (base PCH without Vulkan/ImGui)
target_precompile_headers(Database PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch_base.h>"
)

# Unity build for faster compilation
set_target_properties(Database PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 8
)
