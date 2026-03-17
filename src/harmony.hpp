#pragma once

#include <filesystem>
#include <print>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------

template<typename... Args>
void log(std::format_string<Args...> fmt, Args&&... args)
{
    std::println(stderr, fmt, std::forward<Args>(args)...);
}

// ---------------------------------------------------------------------------

inline
auto make_symlink(const fs::path& path, const fs::path& target, bool force = false) -> void {
    bool exists = fs::exists(path) || fs::is_symlink(path);

    if (exists) {
        bool same_file = false;
        try {
            same_file = fs::equivalent(path, target);
        } catch (...) {
            log("[link] Error following existing path, attempt to replace...");
        }

        if (same_file) { return; }

        if (fs::is_symlink(path)) {
            fs::remove(path);
        } else if (force) {
            if (fs::is_regular_file(path)) {
                fs::remove(path);
            } else {
                fs::remove_all(path);
            }
        } else {
            throw std::runtime_error(
                std::format("[link] Path {} already exists and is not a symlink.", path.string()));
        }
    }

    log("[link] {} -> {}", path.string(), target.string());
    fs::create_directories(path.parent_path());
    fs::create_symlink(fs::weakly_canonical(target), path);
}
