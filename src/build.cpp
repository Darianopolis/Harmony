#include "build.hpp"

void Build(const cfg::Step& step, const Backend& backend)
{
    (void)backend;

    // 1. Find all sources from roots
    // 2. Compute dependencies
    // 3. Generate compile commands

    for (auto& source : step.sources) {
        for (auto file : fs::recursive_directory_iterator(source.root,
                fs::directory_options::follow_directory_symlink |
                fs::directory_options::skip_permission_denied)) {
            auto ext = file.path().extension();
            if (ext == ".cpp") {
                std::println("C++ source file: {}", file.path().string());
            } else if (ext == ".hpp") {
                std::println("C++ header file: {}", file.path().string());
            }
        }
    }
}
