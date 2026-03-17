#pragma once

#include <filesystem>
#include <string>

namespace harmony::git_cache {

struct CheckoutResult
{
    std::filesystem::path repo;
    std::string oid;
};

// Ensures url@ref is present in the local cache.
// Returns the path to the content-addressed checkout directory.
//
// cache_dir : root of the git cache (e.g. from $GIT_CACHE_DIR)
// url       : git repository URL
// ref       : commit hash (40 hex chars), branch name, or tag
// fetch     : if true, re-fetch branch refs to get the latest commit
//
// Throws std::runtime_error on failure.
auto checkout(
    const std::filesystem::path& cache_dir,
    const std::string& url,
    const std::string& ref,
    bool fetch = false
) -> CheckoutResult;

} // namespace harmony::git_cache
