cmake_minimum_required(VERSION 3.30.3)
project(Harmony LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(
        /Zc:preprocessor
        /Zc:__cplusplus
        /utf-8
        /openmp:llvm)
# ----------------------------------------------------------------------------------------------------------------------
#       Third Party - yyjson
# ----------------------------------------------------------------------------------------------------------------------
include(FetchContent)
fetchcontent_declare(yyjson
        GIT_REPOSITORY https://github.com/ibireme/yyjson.git)
fetchcontent_makeavailable(yyjson)
# ----------------------------------------------------------------------------------------------------------------------
#       Third Party - xxhash
# ----------------------------------------------------------------------------------------------------------------------
fetchcontent_declare(xxhash
        GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
        GIT_TAG origin/dev)
fetchcontent_makeavailable(xxhash)
add_library(xxhash)
target_sources(xxhash
        PUBLIC
        ${xxhash_SOURCE_DIR}/xxhash.c)
target_include_directories(xxhash
        PUBLIC
        ${xxhash_SOURCE_DIR})
# ----------------------------------------------------------------------------------------------------------------------
#       Harmony
# ----------------------------------------------------------------------------------------------------------------------
add_executable(harmony)
target_sources(harmony
        PUBLIC
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
        src/generators/cmake-generator.hpp
        src/generators/cmake-generator.cpp)
target_include_directories(harmony
        PRIVATE
        src)
target_link_libraries(harmony
        PUBLIC
        yyjson
        xxhash)
set_target_properties(harmony PROPERTIES LINKER_LANGUAGE CXX)
add_custom_command(TARGET harmony POST_BUILD COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:harmony> ${CMAKE_SOURCE_DIR}/out/harmony.exe)
# ----------------------------------------------------------------------------------------------------------------------
#       Test
# ----------------------------------------------------------------------------------------------------------------------
add_executable(test)
target_sources(test
        PUBLIC
        test/main.cpp
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        test/other.ixx
        test/third.ixx)
set_target_properties(test PROPERTIES LINKER_LANGUAGE CXX)
add_custom_command(TARGET test POST_BUILD COMMAND
        ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test> ${CMAKE_SOURCE_DIR}/out/test.exe)
