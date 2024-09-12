#include "build.hpp"

void Build(const cfg::Step& step, const Backend& backend)
{
    // 1. Find all sources from roots
    // 2. Compute dependencies
    // 3. Generate compile commands

    std::vector<Source> sources;

    for (auto& source : step.sources) {
        for (auto file : fs::recursive_directory_iterator(source.root,
                fs::directory_options::follow_directory_symlink |
                fs::directory_options::skip_permission_denied)) {

            auto ext = file.path().extension();
            if      (ext == ".cpp") sources.emplace_back(file, SourceType::CppSource);
            else if (ext == ".hpp") sources.emplace_back(file, SourceType::CppHeader);
            else if (ext == ".ixx") sources.emplace_back(file, SourceType::CppInterface);
        }
    }

    for (auto& source : sources) {
        switch (source.type) {
            break;case SourceType::CppSource:    std::println("C++ Source    - {}", source.path.string());
            break;case SourceType::CppHeader:    std::println("C++ Header    - {}", source.path.string());
            break;case SourceType::CppInterface: std::println("C++ Interface - {}", source.path.string());
        }
    }

    std::vector<std::string> dependency_info;
    backend.FindDependencies(sources, dependency_info);

    for (int i = 0; i < sources.size(); ++i) {
        std::println("Dependency info for [{}]:", sources[i].path.string());
        std::println("{}", dependency_info[i]);
    }
}
