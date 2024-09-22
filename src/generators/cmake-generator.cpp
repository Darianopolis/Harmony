#include "cmake-generator.hpp"

#ifndef HARMONY_USE_IMPORT_STD
#include <ranges>
#endif

void GenerateCMake(const fs::path& output_dir, std::vector<Task>& tasks, std::unordered_map<std::string, Target>& targets)
{
    // TODO: CMake doesn't allow files to be referenced that don't share a common path with the CmakeLists.txt they are included from
    //   Solution: Generate symbolic/hard links to trick CMake?
    //   Solution: Generate additional CMakeLists.txt and include them?

    std::ofstream out(output_dir / "CMakeLists.txt.generated", std::ios::binary);

    out << R"(cmake_minimum_required(VERSION 3.30.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
        # This specific value changes as experimental support evolves. See
        # `Help/dev/experimental.rst` in the CMake source corresponding to
        # your CMake build for the exact value to use.
        "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")
project(harmony_generated LANGUAGES CXX C)
set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(
        /Zc:preprocessor
        /Zc:__cplusplus
        /utf-8
        /openmp:llvm)
)";

    for (auto&[name, target] : targets) {

        out << "# ------------------------------------------------------------------------------\n";

        if (target.executable) {
            out << "add_executable(" << name << ")\n";
        } else {
            out << "add_library(" << name << ")\n";
        }

        if (!target.sources.empty()) {

            std::vector<std::string>* defines = nullptr;
            std::vector<fs::path>* includes = nullptr;

            out << "target_sources(" << name << "\n";
            for (uint32_t i = 0; i < 2; ++i) {
                bool is_modules_pass = i;
                if (is_modules_pass) {
                    out << "        PUBLIC\n";
                    out << "        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES\n";
                } else {
                    out << "        PUBLIC\n";
                }

                bool found_any_modules = false;

                for (auto& task : tasks) {
                    if (task.target != &target) continue;

                    if (is_modules_pass) {
                        if (task.source.type != SourceType::CppInterface) continue;
                        if (task.produces.empty()) {
                            // CMake freaks out if a interface unit in CXX_MODULES doesn't export a module
                            continue;
                        }
                    } else {
                        if (task.source.type == SourceType::CppInterface) {
                            found_any_modules = true;
                            continue;
                        }
                    }

                    defines = &task.defines;
                    includes = &task.include_dirs;

                    out << "        " << FormatPath(task.source.path) << "\n";
                }

                if (!found_any_modules) {
                    break;
                }
            }
            out << "        )\n";

            // Scan for sources with different source types
            // TODO: How to prevent these from override "leaking" out
            for (auto& task : tasks) {
                if (task.target != &target) continue;
                if (task.source.type == task.source.detected_type) continue;

                // Headers don't need to be overriden here
                if (task.source.type == SourceType::CppHeader) continue;
                // Interfaces are already explicitly marked with the CXX_MODULE file set
                if (task.source.type == SourceType::CppInterface) continue;

                // See: https://cmake.org/cmake/help/latest/prop_sf/LANGUAGE.html
                out << "set_source_files_properties(" << FormatPath(task.source.path) << " PROPERTIES LANGUAGE CXX)\n";
            }

            if (includes && !includes->empty()) {
                out << "target_include_directories(" << name << '\n';
                out << "        PRIVATE\n";
                for (auto& include : *includes) {
                    out << "        " << FormatPath(include) << '\n';
                }
                out << "        )\n";
            }

            if (defines && !defines->empty()) {
                // TODO: These seem to be leaking back into linked targets??
                out << "target_compile_definitions(" << name << '\n';
                out << "        PRIVATE\n";
                for (auto& define : *defines) {
                    out << "        " << define << '\n';
                }
                out << "        )\n";
            }

            if (!target.import.empty()) {
                out << "target_link_libraries(" << name << '\n';
                for (auto& import : target.import) {
                    out << "        " << import << '\n';
                }

                // TODO: THIS IS DUPLICATE AND MSVC SPECIFIC CODE
                auto AddLinks = [&](const Target& t) {
                    LogTrace("Adding links for: [{}]", t.name);
                    for (auto& link : t.links) {
                        if (fs::is_regular_file(link)) {
                            LogTrace("    adding: [{}]", link.string());
                            out << "        ${CMAKE_SOURCE_DIR}/" << FormatPath(link) << '\n';
                        } else if (fs::is_directory(link)) {
                            LogTrace("  finding links in: [{}]", link.string());
                            for (auto iter : fs::directory_iterator(link)) {
                                auto path = iter.path();
                                if (path.extension() == ".lib") {
                                    LogTrace("    adding: [{}]", path.string());
                                    out << "        ${CMAKE_SOURCE_DIR}/" << FormatPath(path) << '\n';
                                }
                            }
                        } else {
                            LogWarn("link path not found: [{}]", link.string());
                        }
                    }
                };
                AddLinks(target);
                for (auto* import_target : target.flattened_imports) {
                    AddLinks(*import_target);
                }

                out << "        )\n";
            }
        }
    }
}
