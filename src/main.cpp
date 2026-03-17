#include "harmony.hpp"

#include "git-cache.hpp"

using namespace std::literals;

int main(int argc, char* argv[])
{
    auto cache_dir = std::filesystem::path(getenv("HOME")) / ".git-cache";

    if (argc < 4 || (argc > 1 && "git"sv != argv[1])) {
        log("Usage: git <url> <ref> [<fetch>]");
        return 1;
    }

    bool fetch = argc > 4 && "fetch"sv == argv[4];

    auto[dir, oid] = harmony::git_cache::checkout(
        cache_dir,
        argv[2],
        argv[3],
        fetch
        );

    std::println("{}", dir.c_str());
    std::println("{}", oid);
}
