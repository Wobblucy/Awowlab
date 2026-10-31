cmake_minimum_required(VERSION 3.20) # bump for FetchContent + VS2022

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  # Edit-and-continue (/ZI) for Debug only. RelWithDebInfo gets a plain
  # program database (/Zi): it is an optimized build, and the release presets
  # turn on link-time optimization, whose /GL cannot be combined with /ZI -
  # the stock CMake template asks for both here and RelWithDebInfo then fails
  # to compile at all with "D8016: '/ZI' and '/GL' are incompatible".
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>:$<$<CONFIG:Debug>:EditAndContinue>$<$<CONFIG:RelWithDebInfo>:ProgramDatabase>>")
endif()

project("AwowLab")

# Compiler-specific warning flags for stricter compliance
if (MSVC)
    # Strict compilation: warnings as errors, high warning level, strict standard compliance
    # /utf-8: Treat source files as UTF-8 and use UTF-8 for string literals (required for CJK characters)
    add_compile_options(/W4 /WX /permissive- /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # GCC/Clang: Enable strict warnings
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Dependencies
include(FetchContent)

FetchContent_Declare(
    portable-file-dialogs
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG 0.1.0
)
FetchContent_MakeAvailable(portable-file-dialogs)

# Subprojects
add_subdirectory("AwowLab")

# Tests
option(BUILD_TESTS "Build unit tests" ON)
if (BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.0
    )
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    add_subdirectory("tests")
endif()