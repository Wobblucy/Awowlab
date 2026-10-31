cmake_minimum_required(VERSION 3.20)
project(warcraftlogs_manager LANGUAGES CXX)

find_package(RapidJSON CONFIG REQUIRED)

add_library(warcraftlogs_manager STATIC
    # Auth
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Auth/OAuthClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Auth/UserOAuthClient.cpp

    # Api
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Api/GraphQLClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Api/RateLimiter.cpp

    # Adapter
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Adapter/UrlParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Adapter/GuidGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Models/SeasonZones.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Models/RankingSample.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Adapter/WclToAwowAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Adapter/WclToAwowAdapter_Combat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Adapter/WclToAwowAdapter_Lifecycle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Adapter/WclToAwowAdapter_Cast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Adapter/WclToAwowAdapter_Position.cpp

    # Storage
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Storage/ResponseCache.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/src/WarcraftLogsManager.cpp
)

target_include_directories(warcraftlogs_manager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${RAPIDJSON_INCLUDE_DIRS}
    PRIVATE
        # WclToAwowAdapter writes AwowLab's own event structures.
        ${CMAKE_CURRENT_SOURCE_DIR}/../Database
        ${CMAKE_CURRENT_SOURCE_DIR}/../Parser/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # Core/StringInterner.h
)

target_link_libraries(warcraftlogs_manager
    PUBLIC
        warcraftlogs_http
        awowlab_build_options  # WarcraftLogs client id (AWOWLAB_WCL_CLIENT_ID)
)

set_property(TARGET warcraftlogs_manager PROPERTY CXX_STANDARD 23)

if(WIN32)
    target_compile_definitions(warcraftlogs_manager PRIVATE
        _WIN32_WINNT=0x0A00  # Windows 10
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Shares the base PCH, which leaves out Vulkan and ImGui.
target_precompile_headers(warcraftlogs_manager PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch_base.h>"
)
