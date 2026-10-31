cmake_minimum_required(VERSION 3.20)
project(warcraftlogs_http LANGUAGES CXX)

find_package(Boost CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(warcraftlogs_http STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HttpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LocalHttpServer.cpp
)

target_include_directories(warcraftlogs_http
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # Core/ErrorLogger.h
)

target_link_libraries(warcraftlogs_http
    PUBLIC
        Boost::boost
        OpenSSL::SSL
        OpenSSL::Crypto
)

# crypt32 reads the Windows certificate store for TLS peer verification.
if(WIN32)
    target_link_libraries(warcraftlogs_http PRIVATE crypt32)
endif()

set_property(TARGET warcraftlogs_http PROPERTY CXX_STANDARD 23)

if(WIN32)
    target_compile_definitions(warcraftlogs_http PRIVATE
        _WIN32_WINNT=0x0A00  # Windows 10
        _CRT_SECURE_NO_WARNINGS
    )
endif()
