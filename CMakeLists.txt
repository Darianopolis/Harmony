cmake_minimum_required(VERSION 3.30.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
        # This specific value changes as experimental support evolves. See
        # `Help/dev/experimental.rst` in the CMake source corresponding to
        # your CMake build for the exact value to use.
        "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")
project(harmony LANGUAGES CXX C)
set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(
        /Zc:preprocessor
        /Zc:__cplusplus
        /utf-8
        /openmp:llvm)
# ----------------------------------------------------------------------------------------------------------------------
#       Dependencies
# ----------------------------------------------------------------------------------------------------------------------
include(FetchContent)
fetchcontent_declare(yyjson
        GIT_REPOSITORY https://github.com/ibireme/yyjson.git)
fetchcontent_makeavailable(yyjson)
fetchcontent_declare(xxhash
        GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
        GIT_TAG origin/dev)
fetchcontent_makeavailable(xxhash)
# ----------------------------------------------------------------------------------------------------------------------
#       Harmony
# ----------------------------------------------------------------------------------------------------------------------
add_executable(harmony
        src/backend/clangcl-backend.cpp
        src/backend/msvc-backend.cpp
        src/build-scan.cpp
        src/build.cpp
        src/cli.cpp
        src/configuration.cpp
        src/core.cpp
        src/json.cpp
        src/backend/msvc-common.cpp
        src/log.cpp
        ${xxhash_SOURCE_DIR}/xxhash.c
        src/generators/cmake-generator.hpp
        src/generators/cmake-generator.cpp)
target_include_directories(harmony
        PRIVATE
        src
        ${yyjson_SOURCE_DIR}/src
        ${xxhash_SOURCE_DIR})
target_link_libraries(harmony
        PUBLIC
        yyjson)
set_target_properties(harmony PROPERTIES LINKER_LANGUAGE CXX)
add_custom_command(TARGET harmony POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:harmony>
        ${CMAKE_SOURCE_DIR}/out/harmony.exe)
# ----------------------------------------------------------------------------------------------------------------------
#       Test
# ----------------------------------------------------------------------------------------------------------------------
add_executable(test)
target_sources(test
        PUBLIC
        test/main.cpp
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        test/other.ixx)
set_target_properties(test PROPERTIES LINKER_LANGUAGE CXX)
add_custom_command(TARGET test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test>
        ${CMAKE_SOURCE_DIR}/out/test.exe)
