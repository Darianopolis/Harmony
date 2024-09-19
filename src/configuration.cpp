#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <configuration.hpp>
#include <json.hpp>

#include <curl/curl.h>

#ifndef HARMONY_USE_IMPORT_STD
#include <thread>
#include <unordered_map>
#include <unordered_set>
#endif

void ParseConfig(std::string_view input, std::vector<Task>& tasks, std::unordered_map<std::string, Target>& out_targets)
{
    LogInfo("Parsing config");

    uint32_t source_id = 0;

    auto deps_folder = fs::path(".deps");
    JsonDocument doc(input);

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

        auto& out_target = out_targets[name];
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
                    Error(std::format("Unknown executable type: [{}]", type_str));
                }()
            );
        }
    }

    LogInfo("Parsed json config, generating tasks");

    // for (auto&[name, target] : out_targets) {
    for (auto in_target :  doc.root()["targets"]) {
        auto name = in_target["name"].string();
        auto& target = out_targets.at(name);

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
                    auto& imported = out_targets.at(import_name);

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

                if      (ext == ".c") type = SourceType::CSource;
                else if (ext == ".cpp") type = SourceType::CppSource;
                else if (ext == ".hpp") type = SourceType::CppHeader;
                else if (ext == ".ixx") type = SourceType::CppInterface;

                if (type == SourceType::Unknown) return;

                auto& task = tasks.emplace_back();
                task.target = &target;
                task.source = { file, type };

                // TODO: We really don't want to duplicate this for every task
                task.include_dirs = includes;
                task.defines = defines;

                task.unique_name = std::format("{}.{}.{}", target.name, task.source.path.filename().replace_extension("").string(), source_id++);

                switch (task.source.type) {
                    break;case SourceType::CppSource:    LogTrace("C++ Source    - {}", task.source.path.string());
                    break;case SourceType::CppHeader:    LogTrace("C++ Header    - {}", task.source.path.string());
                    break;case SourceType::CppInterface: LogTrace("C++ Interface - {}", task.source.path.string());
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

std::string QuotedAbsPath(const fs::path path)
{
    auto str = fs::absolute(path).string();
    return std::format("\"{}\"", str);
}

void Fetch(std::string_view config, bool clean)
{
    auto deps_folder = fs::path(".deps");
    fs::create_directories(deps_folder);

    JsonDocument doc(config);

    constexpr uint32_t stage_fetch = 0;
    constexpr uint32_t stage_unpack = 1;
    constexpr uint32_t stage_git_prepare = 2;
    constexpr uint32_t stage_git_build = 3;

    for (uint32_t stage = 0; stage < 4; ++stage) {

        switch (stage) {
            break;case stage_fetch:       LogDebug("Stage: fetch");
            break;case stage_unpack:      LogDebug("Stage: unpack");
            break;case stage_git_prepare: LogDebug("Stage: git prepare");
            break;case stage_git_build:   LogDebug("Stag: git build");
        }

        std::vector<std::jthread> tasks;

        for (auto target : doc.root()["targets"]) {
            auto name = target["name"].string();
            auto dir = deps_folder / name;

            // tasks.emplace_back([=] {

                if (auto _git = target["git"]) {
                    if (stage == stage_fetch) {

                        std::string url;
                        std::optional<std::string> branch;
                        if (_git.string()) {
                            url = _git.string();
                        } else {
                            url = _git["url"].string();
                            branch = _git["branch"].string();
                        }

                        if (fs::exists(dir)) {
                            LogDebug("Checking for git updates in [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format("cd {}", dir.string());
                            if (branch) {
                                cmd += std::format(" && git checkout {}", *branch);
                            }
                            if (clean) {
                                cmd += std::format(" && git reset --hard");
                            }
                            cmd += " && git pull";

                            LogCmd(cmd);

                            std::system(cmd.c_str());
                        } else {
                            LogDebug("Cloning into [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format("git clone {} --depth=1 --recursive", url);
                            if (branch) {
                                cmd += std::format(" --branch={}", *branch);
                            }
                            cmd += std::format(" .deps/{}", name);

                            LogCmd(cmd);

                            std::system(cmd.c_str());
                        }
                    }

                } else if (auto download = target["download"]) {
                    auto url = download["url"].string();
                    auto type = download["type"].string();

                    auto tmp_file = dir.string() + ".tmp";

                    if (!fs::exists(dir) || clean) {
                        LogInfo("Downloading [{}.tmp] <- [{}]", dir.string(), url);

                        if (stage == stage_fetch) {
                            HARMONY_DO_ONCE() { curl_global_init(CURL_GLOBAL_ALL); };
                            HARMONY_ON_EXIT() { curl_global_cleanup(); };

                            auto curl = curl_easy_init();

                            curl_easy_setopt(curl, CURLOPT_URL, url);
                            // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
                            // curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
                            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
                            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                            auto file = fopen(tmp_file.c_str(), "wb");
                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);


                            curl_easy_perform(curl);

                            curl_easy_cleanup(curl);

                            fclose(file);
                        }

                        if (stage == stage_unpack) {
                            LogInfo("Unpacking [{0}] <- [{0}.tmp]", dir.string());

                            if (type && "zip"sv == type) {
                                std::string cmd;
                                cmd += std::format(" cd .deps && 7z x -y {0}.tmp -o{0}", name);

                                LogCmd(cmd);
                                std::system(cmd.c_str());
                            }

                            fs::remove(tmp_file);
                        }
                    }
                }

                if (auto cmake = target["cmake"]) {
                    (void)cmake;

                    auto cmake_build_dir = ".harmony-cmake-build";

                    auto profile = "RelWithDebInfo";

                    if (stage == stage_git_prepare) {
                        // TODO: Check for for successful completion of cmake configure instead of fs::exists
                        if (!fs::exists(dir / cmake_build_dir) || clean) {
                            LogInfo("Configuring CMake build in [{}]", dir.string());

                            std::string cmd;
                            cmd += std::format(" cd .deps/{}", name);
                            cmd += std::format(" && cmake . -DCMAKE_INSTALL_PREFIX={0}/install -DCMAKE_BUILD_TYPE={1} -B {0}", cmake_build_dir, profile);

                            for (auto option : cmake["options"]) {
                                cmd += std::format(" -D{}", option.string());
                            }

                            LogCmd(cmd);
                            std::system(cmd.c_str());
                        }
                    }

                    if (stage == stage_git_build) {
                        LogInfo("Running CMake build in [{}]", dir.string());

                        std::string cmd;
                        cmd += std::format(" cd .deps/{}", name);
                        cmd += std::format(" && cmake --build {} --config {} --target install --parallel 32", cmake_build_dir, profile);

                        LogCmd(cmd);
                        std::system(cmd.c_str());
                    }
                }
            // });
        }
    }

    LogInfo("==== Complete ====");
}
