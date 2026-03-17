#include <print>

#include "git-cache.hpp"

int main()
{
    std::println("Hello, world!");

    auto dir = harmony::git_cache::checkout(
        std::filesystem::path(".harmony"),
        ".local/test-repo",
        "main",
        false
        );

    std::println("checked out: {}", dir.c_str());
}
