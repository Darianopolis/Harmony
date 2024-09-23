#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>
#include <json.hpp>

#ifndef HARMONY_USE_IMPORT_STD
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#endif

void ParseConfig(std::string_view config, BuildState& state)
{
    LogInfo("Generating initial build tasks");

    auto deps_folder = fs::path(".deps");
    JsonDocument doc(config);

    for (auto in_target :  doc.root()["targets"]) {
        auto name = in_target["name"].string();
        LogTrace("name = {}", (bool)name);
        auto dir = deps_folder / name;
        if (auto dir_str = in_target["dir"].string()) {
            LogTrace("Custom dir path: {}", dir_str);
            dir = dir_str;
        }

        LogTrace("target[{}]", name);
        LogTrace("  dir = {}", dir.string());

        auto& out_target = state.targets[name];
        out_target.name = name;

        for (auto source : in_target["sources"]) {
            if (source.obj()) {
                LogTrace("  sources(type = {})", source["type"].string());

                auto type = [&] {
                    auto type_str = source["type"].string();
                    if (!type_str) Error("Source object must contain 'type'");
                    if ("c++"sv == type_str) return SourceType::CppSource;
                    if ("c++header"sv == type_str) return SourceType::CppHeader;
                    if ("c++interface"sv == type_str) return SourceType::CppInterface;
                    Error(std::format("Unknown source type: [{}]", type_str));
                }();

                for (auto file : source["paths"]) {
                    LogTrace("    {}", file.string());
                    out_target.sources.emplace_back(dir / file.string(), type);
                }
            } else {
                LogTrace("  source: {}", source.string());
                out_target.sources.emplace_back(dir / source.string(), SourceType::Unknown);
            }
        }

        for (auto include : in_target["include"]) {
            out_target.include_dirs.emplace_back(dir / include.string());
        }

        for (auto define : in_target["define"]) {
            out_target.define_build.emplace_back(define.string());
            out_target.define_import.emplace_back(define.string());
        }

        for (auto define : in_target["define-build"]) {
            out_target.define_build.emplace_back(define.string());
        }

        for (auto define : in_target["define-import"]) {
            out_target.define_import.emplace_back(define.string());
        }

        for (auto import : in_target["import"]) {
            out_target.import.emplace_back(import.string());
        }

        for (auto link : in_target["link"]) {
            out_target.links.emplace_back(dir / link.string());
        }

        if (auto executable = in_target["executable"]) {
            out_target.executable.emplace(
                executable["name"].string(),
                [&] {
                    auto type_str = executable["type"].string();
                    if (!type_str) Error("Executable must specify type [console] or [window]");
                    if ("console"sv == type_str) return ExecutableType::Console;
                    if ("window"sv == type_str) return ExecutableType::Window;
                    Error(std::format("Unknown executable type: [{}] (expected [console] or [window])", type_str));
                }()
            );
        }

        if (auto in_git = in_target["git"]) {
            auto& git = out_target.git.emplace();
            if (in_git.string()) {
                git.url = in_git.string();
            } else {
                git.url = in_git["url"].string();
                if (auto branch = in_git["branch"]) {
                    git.branch = branch.string();
                }
            }
        }

        if (auto in_download = in_target["download"]) {
            auto& download = out_target.download.emplace();
            download.url = in_download["url"].string();
            if (auto type = in_download["type"].string()) {
                if ("zip"sv == type) download.type = ArchiveType::Zip;
                else Error("Unknown download type: {}", type);
            }
        }

        if (auto in_cmake = in_target["cmake"]) {
            auto& cmake = out_target.cmake.emplace();
            for (auto option : in_cmake["options"]) {
                cmake.options.emplace_back(option.string());
            }
        }
    }
}

void ExpandTargets(BuildState& state)
{
    uint32_t source_id = 0;

    for (auto&[_, target] : state.targets) {
        if (target.sources.empty()) continue;

        LogTrace("====");

        std::vector<fs::path> includes;
        std::vector<std::string> defines;

        for (auto& include_dir : target.include_dirs) {
            includes.emplace_back(include_dir);
        }
        for (auto& define : target.define_build) {
            defines.emplace_back(define);
        }

        for (auto& include : includes) {
            LogTrace("  includes: {}", include.string());
        }

        for (auto& define : defines) {
            LogTrace("  defines:  {}", define);
        }

        std::unordered_set<Target*> flattened;

        [&](this auto&& self, Target& cur) -> void {
            if (flattened.contains(&cur)) return;
            flattened.emplace(&cur);

            for (auto import_name : cur.import) {
                LogTrace("  importing from [{}]", import_name);
                try {
                    auto& imported = state.targets.at(import_name);

                    for (auto& include_dir : imported.include_dirs) {
                        LogTrace("    includes: {}", include_dir.string());
                        includes.emplace_back(include_dir);
                    }

                    for (auto& define : imported.define_import) {
                        LogTrace("    defines:  {}", define);
                        defines.emplace_back(define);
                    }

                    self(imported);
                } catch (std::exception& e) {
                    Error(e.what());
                }
            }
        }(target);

        target.flattened_imports = std::move(flattened);

        for (auto& source : target.sources) {
            auto AddSourceFile = [&](const fs::path& file, SourceType type) {
                auto ext = file.extension();

                auto detected_type = SourceType::Unknown;

                if      (ext == ".c") detected_type = SourceType::CSource;
                else if (ext == ".cpp") detected_type = SourceType::CppSource;
                else if (ext == ".hpp") detected_type = SourceType::CppHeader;
                else if (ext == ".ixx") detected_type = SourceType::CppInterface;
                else if (ext == ".cppm") detected_type = SourceType::CppInterface;

                if (type == SourceType::Unknown) {
                    type = detected_type;
                }

                if (type == SourceType::Unknown) return;

                auto& task = state.tasks.emplace_back();
                task.target = &target;
                task.source = { file, type, detected_type };

                // TODO: We really don't want to duplicate this for every task
                task.include_dirs = includes;
                task.defines = defines;

                // TODO: These source ids aren't stable across source file add/remove/reorder
                task.unique_name = std::format("{}.{}.{}", target.name, task.source.path.filename().replace_extension("").string(), source_id++);

                switch (task.source.type) {
                    break;case SourceType::Unknown: std::unreachable();
                    break;case SourceType::CppSource:    LogTrace("C++ Source    - {}", task.source.path.string());
                    break;case SourceType::CppHeader:    LogTrace("C++ Header    - {}", task.source.path.string());
                    break;case SourceType::CppInterface: LogTrace("C++ Interface - {}", task.source.path.string());
                    break;case SourceType::CSource:      LogTrace("C   Source    - {}", task.source.path.string());
                }
            };

            if (fs::is_directory(source.path)) {
                LogTrace("  scanning for source in: [{}]", source.path.string());
                for (auto file : fs::recursive_directory_iterator(source.path,
                        fs::directory_options::follow_directory_symlink |
                        fs::directory_options::skip_permission_denied)) {

                    AddSourceFile(file.path(), source.type);
                }
            } else if (fs::is_regular_file(source.path)) {
                AddSourceFile(source.path, source.type);
            } else {
                LogTrace("Source path [{}] not dir or file", source.path.string());
            }
        }
    }
}

void Fetch(BuildState& state, bool clean, bool update)
{
    update |= clean;

    if (clean) {
        LogInfo("Performing clean fetch of dependencies");
    } else if (update) {
        LogInfo("Checking for dependency updates");
    } else {
        LogInfo("Checking for missing dependencies");
    }

    auto deps_folder = fs::path(".deps");
    fs::create_directories(deps_folder);

    constexpr uint32_t stage_fetch = 0;
    constexpr uint32_t stage_unpack = 1;
    constexpr uint32_t stage_git_prepare = 2;
    constexpr uint32_t stage_git_build = 3;

    std::unordered_set<fs::path> cmake_do_build;

    for (uint32_t stage = 0; stage < 4; ++stage) {

        switch (stage) {
            break;case stage_fetch:       LogDebug("Stage: fetch");
            break;case stage_unpack:      LogDebug("Stage: unpack");
            break;case stage_git_prepare: LogDebug("Stage: git prepare");
            break;case stage_git_build:   LogDebug("Stag: git build");
        }

        std::vector<std::jthread> tasks;

        for (auto[name, target] : state.targets) {
            auto dir = deps_folder / name;

            auto task = [=, &cmake_do_build] {
                if (auto& git = target.git) {

                    if (stage == stage_fetch) {
                        if (!fs::exists(dir)) {
                            LogDebug("Cloning into [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format("git clone {} --depth=1 --recursive", git->url);
                            if (git->branch) {
                                cmd += std::format(" --branch={}", *git->branch);
                            }
                            cmd += std::format(" .deps/{}", name);

                            LogCmd(cmd);

                            std::system(cmd.c_str());
                        } else if (update) {
                            LogDebug("Checking for git updates in [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format("cd {}", dir.string());
                            if (git->branch) {
                                cmd += std::format(" && git checkout {}", *git->branch);
                            }
                            if (clean) {
                                cmd += std::format(" && git reset --hard");
                            }
                            cmd += " && git pull";

                            LogCmd(cmd);

                            std::system(cmd.c_str());
                        }
                    }

                } else if (auto& download = target.download) {
                    auto tmp_file = dir.string() + ".tmp";

                    if (!fs::exists(dir) || update) {
                        LogInfo("Downloading [{}.tmp] <- [{}]", dir.string(), download->url);

                        if (stage == stage_fetch) {
                            auto cmd = std::format("curl -o {} {}", tmp_file, download->url);
                            LogCmd(cmd);
                            std::system(cmd.c_str());
                        }

                        if (stage == stage_unpack) {
                            LogInfo("Unpacking [{0}] <- [{0}.tmp]", dir.string());

                            if (download->type == ArchiveType::Zip) {
                                std::string cmd;
                                cmd += std::format(" cd .deps && 7z x -y {0}.tmp -o{0}", name);

                                LogCmd(cmd);
                                std::system(cmd.c_str());
                            }

                            fs::remove(tmp_file);
                        }
                    }
                }

                if (auto& cmake = target.cmake) {
                    auto cmake_build_dir = ".harmony-cmake-build";

                    auto profile = "RelWithDebInfo";

                    if (stage == stage_git_prepare) {
                        // TODO: Check for for successful completion of cmake configure instead of fs::exists
                        if (!fs::exists(dir / cmake_build_dir) || update) {
                            LogInfo("Configuring CMake build in [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format(" cd .deps/{}", name);
                            cmd += std::format(" && cmake . -DCMAKE_INSTALL_PREFIX={0}/install -DCMAKE_BUILD_TYPE={1} -B {0}", cmake_build_dir, profile);

                            for (auto option : cmake->options) {
                                cmd += std::format(" -D{}", option);
                            }

                            LogCmd(cmd);
                            std::system(cmd.c_str());
                            cmake_do_build.emplace(dir);
                        }
                    }

                    if (stage == stage_git_build) {
                        if (cmake_do_build.contains(dir) || update) {
                            LogInfo("Running CMake build in [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format(" cd .deps/{}", name);
                            cmd += std::format(" && cmake --build {} --config {} --target install --parallel 32", cmake_build_dir, profile);

                            LogCmd(cmd);
                            std::system(cmd.c_str());
                        }
                    }
                }
            };

            if (stage != stage_git_build) {
                tasks.emplace_back(task);
            } else {
                task();
            }
        }
    }
}
