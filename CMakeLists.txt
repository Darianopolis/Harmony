cmake_minimum_required(VERSION 4.0)
project(harmony)

# ------------------------------------------------------------------------------

find_package(OpenSSL REQUIRED)
find_package(libgit2 REQUIRED)

# ------------------------------------------------------------------------------

add_executable(harmony)
target_compile_options(harmony PUBLIC
    -std=c++26
    )
target_sources(harmony PUBLIC
    src/main.cpp
    src/git-cache.cpp
    )
target_include_directories(harmony PRIVATE src)
target_link_libraries(harmony PRIVATE OpenSSL::Crypto libgit2::libgit2package)
