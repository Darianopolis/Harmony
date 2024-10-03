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

const static fs::path CMakeBuildDir = ".harmony-cmake-build";
const static fs::path CMakeInstallDir = CMakeBuildDir / "install";
const static fs::path CMakeInstallBinDir = CMakeInstallDir / "bin";
const static fs::path CMakeInstallIncludeDir = CMakeInstallDir / "include";
const static fs::path CMakeInstallLinkDir = CMakeInstallDir / "link";

void ParseTargetsFile(BuildState& state, std::string_view config)
{
    LogInfo("Parsing targets file");

    auto deps_folder = HarmonyDataDir;
    fs::create_directories(deps_folder);
    JsonDocument doc(config);

    for (auto in_target :  doc.root()["targets"]) {
        auto name = in_target["name"].string();
        auto dir = deps_folder / name;
        if (auto dir_str = in_target["dir"].string()) {
            LogTrace("Custom dir path: {}", dir_str);
            dir = dir_str;
        }

        LogTrace("target[{}]", name);
        LogTrace("  dir = {}", dir.string());

        auto& out_target = state.targets[name];
        out_target.name = name;
        out_target.dir = dir;

        for (auto include : in_target["include"]) {
            out_target.exported_translation_inputs.include_dirs.emplace_back(dir / include.string());
        }

        for (auto define : in_target["define"]) {
            out_target.exported_translation_inputs.defines.emplace_back(define.string());
        }

        for (auto shared : in_target["shared"]) {
            out_target.shared.emplace_back(dir / shared.string());
        }

        SourceSet default_source_set;
        default_source_set.inputs = out_target.exported_translation_inputs;

        for (auto source : in_target["sources"]) {
            if (source.obj()) {
                auto type_str = source["type"].string();
                if (type_str) {
                    LogTrace("  sources(type = {})", type_str);
                } else {
                    LogTrace("  sources");
                }

                auto type = [&] {
                    if (!type_str) return SourceType::Unknown;
                    if ("c"sv == type_str) return SourceType::CSource;
                    if ("c++"sv == type_str) return SourceType::CppSource;
                    if ("c++header"sv == type_str) return SourceType::CppHeader;
                    if ("c++interface"sv == type_str) return SourceType::CppInterface;
                    Error(std::format("Unknown source type: [{}]", type_str));
                }();

                auto& source_set = out_target.sources.emplace_back();
                source_set.inputs = out_target.exported_translation_inputs;
                source_set.inputs.type = type;

                if (auto includes = source["includes"]) {
                    source_set.inputs.include_dirs.clear();
                    for (auto include : includes) {
                        source_set.inputs.include_dirs.emplace_back(dir / include.string());
                    }
                }

                if (auto defines = source["define"]) {
                    source_set.inputs.defines.clear();
                    for (auto define : defines) {
                        source_set.inputs.defines.emplace_back(define.string());
                    }
                }

                for (auto file : source["paths"]) {
                    LogTrace("    {}", file.string());
                    source_set.sources.emplace_back(dir / file.string());
                }
            } else {
                LogTrace("  source: {}", source.string());
                default_source_set.sources.emplace_back(dir / source.string());
            }
        }

        if (!default_source_set.sources.empty()) {
            out_target.sources.emplace_back(std::move(default_source_set));
        }

        for (auto import : in_target["import"]) {
            out_target.imported_targets[import.string()] = DependencyType::Private;
        }

        for (auto import : in_target["import-public"]) {
            out_target.imported_targets[import.string()] = DependencyType::Public;
        }

        for (auto import : in_target["import-interface"]) {
            out_target.imported_targets[import.string()] = DependencyType::Interface;
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

            if (auto include_range = in_cmake["include"]) {
                for (auto include : include_range) {
                    out_target.exported_translation_inputs.include_dirs.emplace_back(dir / CMakeInstallDir / include.string());
                }
            } else {
                out_target.exported_translation_inputs.include_dirs.emplace_back(dir / CMakeInstallIncludeDir);
            }

            if (auto link_range = in_cmake["link"]) {
                for (auto link : link_range) {
                    out_target.links.emplace_back(dir / CMakeInstallDir / link.string());
                }
            } else {
                out_target.links.emplace_back(dir / CMakeInstallLinkDir);
            }

            if (auto shared_range = in_cmake["shared"]) {
                for (auto shared : shared_range) {
                    out_target.shared.emplace_back(dir / CMakeInstallDir / shared.string());
                }
            } else {
                out_target.shared.emplace_back(dir / CMakeInstallBinDir);
            }
        }
    }
}

void ExpandTargets(BuildState& state)
{
    LogInfo("Expanding targets");

    for (auto&[_, target] : state.targets) {

        LogTrace("Expanding target: {}", target.name);

        if (target.sources.empty()) {
            LogTrace("  Target has no sources, skipping...");
            continue;
        }

        TranslationInputs imported_translation_inputs;

        std::unordered_set<Target*> flattened;
        [&](this auto&& self, Target& cur) -> void {
            if (flattened.contains(&cur)) {
                Error("Found recursive dependency on {} (repeated = {})", target.name, cur.name);
            }
            flattened.emplace(&cur);

            for (auto[import_name, type] : cur.imported_targets) {
                LogTrace("  importing from [{}]", import_name);
                try {
                    auto& imported = state.targets.at(import_name);

                    if (   (&cur == &target && type != DependencyType::Interface)
                        || (&cur != &target && type != DependencyType::Private))
                    {
                        for (auto& include_dir : imported.exported_translation_inputs.include_dirs) {
                            LogTrace("    includes: {}", include_dir.string());
                            imported_translation_inputs.include_dirs.emplace_back(include_dir);
                        }

                        for (auto& define : imported.exported_translation_inputs.defines) {
                            LogTrace("    defines:  {}", define);
                            imported_translation_inputs.defines.emplace_back(define);
                        }

                        self(imported);
                    }
                } catch (std::exception& e) {
                    Error(e.what());
                }
            }
        }(target);

        for (auto& source_set : target.sources) {
            LogTrace("  Expanding Source Set");
            // TODO: Memory cleanup
            auto* inputs = new TranslationInputs(source_set.inputs);
            inputs->MergeBack(imported_translation_inputs);

            auto AddSourceFile = [&](const fs::path& file, SourceType type) {
                LogTrace("      Scanning: {} (type = {})", file.string(), SourceTypeToString(type));

                auto ext = file.extension();

                auto detected_type = SourceType::Unknown;

                if      (ext == ".c")    detected_type = SourceType::CSource;
                else if (ext == ".cpp")  detected_type = SourceType::CppSource;
                else if (ext == ".hpp")  detected_type = SourceType::CppHeader;
                else if (ext == ".ixx")  detected_type = SourceType::CppInterface;
                else if (ext == ".cppm") detected_type = SourceType::CppInterface;

                auto effective_type = type == SourceType::Unknown ? detected_type : type;

                if (effective_type == SourceType::Unknown) return;
                LogTrace("        Type = {} (detected = {}, override = {})", SourceTypeToString(effective_type),
                    SourceTypeToString(detected_type), SourceTypeToString(type));

                auto& task = state.tasks.emplace_back();
                task.target = &target;
                task.source = Source(file, detected_type);
                task.inputs = inputs;
            };

            for (auto source : source_set.sources) {
                LogTrace("    Source View: {}", source.path.string());
                if (fs::is_directory(source.path)) {
                    LogTrace("  scanning for source in: [{}]", source.path.string());
                    for (auto file : fs::recursive_directory_iterator(source.path,
                            fs::directory_options::follow_directory_symlink |
                            fs::directory_options::skip_permission_denied)) {

                        AddSourceFile(file.path(), source_set.inputs.type);
                            }
                } else if (fs::is_regular_file(source.path)) {
                    AddSourceFile(source.path, source_set.inputs.type);
                } else {
                    LogTrace("Source path [{}] not dir or file", source.path.string());
                }
            }
        }
    }
}

void FetchExternalData(BuildState& state, bool clean, bool update)
{
    LogInfo("Fetching external data");

    update |= clean;

    if (clean) {
        LogInfo("Performing clean fetch of dependencies");
    } else if (update) {
        LogInfo("Checking for dependency updates");
    } else {
        LogInfo("Checking for missing dependencies");
    }

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
            auto dir = target.dir;

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
                            cmd += std::format(" {}", FormatPath(dir));

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
                                cmd += std::format(" 7z x -y {} -o{}", FormatPath(tmp_file), FormatPath(dir));

                                LogCmd(cmd);
                                std::system(cmd.c_str());
                            }

                            fs::remove(tmp_file);
                        }
                    }
                }

                if (auto& cmake = target.cmake) {

                    auto profile = "RelWithDebInfo";

                    if (stage == stage_git_prepare) {
                        // TODO: Check for for successful completion of cmake configure instead of fs::exists
                        if (!fs::exists(dir / CMakeBuildDir) || update) {
                            LogInfo("Configuring CMake build in [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format(" cd {}", FormatPath(dir));
                            cmd += std::format(" && cmake . -DCMAKE_INSTALL_PREFIX={} -DCMAKE_BUILD_TYPE={} -B {}", FormatPath(CMakeInstallDir), profile, FormatPath(CMakeBuildDir));

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
                            cmd += std::format(" cd {}", FormatPath(dir));
                            cmd += std::format(" && cmake --build {} --config {} --target install --parallel 32", FormatPath(CMakeBuildDir), profile);

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
