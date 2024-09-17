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
#endif

void ParseConfig(std::string_view input, std::vector<Task>& tasks)
{
    std::println("Parsing config");

    uint32_t source_id = 0;

    auto doc = yyjson_read(input.data(), input.size(), 0);
    auto root = yyjson_doc_get_root(doc);

    std::println("root = {}", (void*)root);

    auto deps_folder = fs::path(".deps");

    std::unordered_map<std::string, Target> out_targets;

    for (auto in_target : yyjson_arr_range(yyjson_obj_get(root, "targets"))) {
        auto name = yyjson_get_str(yyjson_obj_get(in_target, "name"));
        auto dir = deps_folder / name;
        if (auto dir_str = yyjson_get_str(yyjson_obj_get(in_target, "dir"))) {
            std::println("Custom dir path: {}", dir_str);
            dir = dir_str;
        }

        std::println("target[{}]", name);
        std::println("  dir = {}", dir.string());

        auto& out_target = out_targets[name];
        out_target.name = name;

        for (auto source : yyjson_arr_range(yyjson_obj_get(in_target, "sources"))) {
            if (yyjson_is_obj(source)) {
                std::println("  sources(type = {})", yyjson_get_str(yyjson_obj_get(source, "type")));

                auto type = [&] {
                    auto type_str = yyjson_get_str(yyjson_obj_get(source, "type"));
                    if (!type_str) error("Source object must contain 'type'");
                    if ("c++"sv == type_str) return SourceType::CppSource;
                    if ("c++header"sv == type_str) return SourceType::CppHeader;
                    if ("c++interface"sv == type_str) return SourceType::CppInterface;
                    error(std::format("Unknown source type: [{}]", type_str));
                }();

                for (auto file : yyjson_arr_range(yyjson_obj_get(source, "paths"))) {
                    std::println("    {}", yyjson_get_str(file));
                    out_target.sources.emplace_back(dir / yyjson_get_str(file), type);
                }
            } else {
                std::println("  source: {}", yyjson_get_str(source));
                out_target.sources.emplace_back(dir / yyjson_get_str(source), SourceType::Unknown);
            }
        }

        for (auto include : yyjson_arr_range(yyjson_obj_get(in_target, "include"))) {
            out_target.include_dirs.emplace_back(dir / yyjson_get_str(include));
        }

        for (auto define : yyjson_arr_range(yyjson_obj_get(in_target, "define"))) {
            out_target.define_build.emplace_back(yyjson_get_str(define));
            out_target.define_import.emplace_back(yyjson_get_str(define));
        }

        for (auto define : yyjson_arr_range(yyjson_obj_get(in_target, "define-build"))) {
            out_target.define_build.emplace_back(yyjson_get_str(define));
        }

        for (auto define : yyjson_arr_range(yyjson_obj_get(in_target, "define-import"))) {
            out_target.define_import.emplace_back(yyjson_get_str(define));
        }

        for (auto import : yyjson_arr_range(yyjson_obj_get(in_target, "import"))) {
            out_target.import.emplace_back(yyjson_get_str(import));
        }
    }

    std::println("Parsed json config, generating tasks");

    for (auto&[name, target] : out_targets) {

        if (target.sources.empty()) continue;

        std::println("====");

        std::vector<fs::path> includes;
        std::vector<std::string> defines;

        for (auto& include_dir : target.include_dirs) {
            includes.emplace_back(include_dir);
        }
        for (auto& define : target.define_build) {
            defines.emplace_back(define);
        }

        for (auto& include : includes) {
            std::println("  includes: {}", include.string());
        }

        for (auto& define : defines) {
            std::println("  defines:  {}", define);
        }

        [&](this auto&& self, const Target& cur) -> void {
            for (auto import_name : cur.import) {
                std::println("  importing from [{}]", import_name);
                try {
                    auto& imported = out_targets.at(import_name);

                    for (auto& include_dir : imported.include_dirs) {
                        std::println("    includes: {}", include_dir.string());
                        includes.emplace_back(include_dir);
                    }

                    for (auto& define : imported.define_import) {
                        std::println("    defines:  {}", define);
                        defines.emplace_back(define);
                    }

                    self(imported);
                } catch (std::exception& e) {
                    error(e.what());
                }
            }
        }(target);

        for (auto& source : target.sources) {
            auto AddSourceFile = [&](const fs::path& file, SourceType type) {
                auto ext = file.extension();

                if      (ext == ".cpp") type = SourceType::CppSource;
                else if (ext == ".hpp") type = SourceType::CppHeader;
                else if (ext == ".ixx") type = SourceType::CppInterface;

                if (type == SourceType::Unknown) return;

                auto& task = tasks.emplace_back();
                task.source = { file, type };

                // TODO: We really don't want to duplicate this for every task
                task.include_dirs = includes;
                task.defines = defines;

                task.unique_name = std::format("{}.{}", task.source.path.filename().replace_extension("").string(), source_id++);

                switch (task.source.type) {
                    break;case SourceType::CppSource:    std::println("C++ Source    - {}", task.source.path.string());
                    break;case SourceType::CppHeader:    std::println("C++ Header    - {}", task.source.path.string());
                    break;case SourceType::CppInterface: std::println("C++ Interface - {}", task.source.path.string());
                }
            };

            if (fs::is_directory(source.path)) {
                std::println("  scanning for source in: [{}]", source.path.string());
                for (auto file : fs::recursive_directory_iterator(source.path,
                        fs::directory_options::follow_directory_symlink |
                        fs::directory_options::skip_permission_denied)) {

                    AddSourceFile(file.path(), source.type);
                }
            } else if (fs::is_regular_file(source.path)) {
                AddSourceFile(source.path, source.type);
            } else {
                std::println("Source path [{}] not dir or file", source.path.string());
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
    auto doc = yyjson_read(config.data(), config.size(), YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS);

    auto root = yyjson_doc_get_root(doc);

    auto deps_folder = fs::path(".deps");
    fs::create_directories(deps_folder);


    curl_global_init(CURL_GLOBAL_ALL);

    constexpr uint32_t stage_fetch = 0;
    constexpr uint32_t stage_unpack = 1;
    constexpr uint32_t stage_git_prepare = 2;
    constexpr uint32_t stage_git_build = 3;

    for (uint32_t stage = 0; stage < 4; ++stage) {

        // phase 0 - git fetch/pull + download
        // phase 1 - unzip
        // phase 3 - git -B
        // phase 4 - git --build

        switch (stage) {
            break;case stage_fetch:       std::println("stage: fetch");
            break;case stage_unpack:      std::println("stage: unpack");
            break;case stage_git_prepare: std::println("stage: git prepare");
            break;case stage_git_build:   std::println("stag: git build");
        }

        std::vector<std::jthread> tasks;

        for (auto target : yyjson_arr_range(yyjson_obj_get(root, "targets"))) {
            auto name = yyjson_get_str(yyjson_obj_get(target, "name"));
            auto dir = deps_folder / name;

            tasks.emplace_back([=] {

                if (auto _git = yyjson_obj_get(target, "git")) {
                    if (stage == stage_fetch) {

                        std::string url;
                        std::optional<std::string> branch;
                        if (yyjson_is_str(_git)) {
                            url = yyjson_get_str(_git);
                        } else {
                            url = yyjson_get_str(yyjson_obj_get(_git, "url"));
                            branch = yyjson_get_str(yyjson_obj_get(_git, "branch"));
                        }

                        if (fs::exists(dir)) {
                            std::string cmd;
                            cmd += std::format("cd {}", dir.string());
                            if (branch) {
                                cmd += std::format(" && git checkout {}", *branch);
                            }
                            if (clean) {
                                cmd += std::format(" && git reset --hard");
                            }
                            cmd += " && git pull";

                            std::println("[cmd] {}", cmd);

                            std::system(cmd.c_str());
                        } else {
                            std::string cmd;
                            cmd += std::format("git clone {} --depth=1 --recursive", url);
                            if (branch) {
                                cmd += std::format(" --branch={}", *branch);
                            }
                            cmd += std::format(" .deps/{}", name);

                            std::println("[cmd] {}", cmd);

                            std::system(cmd.c_str());
                        }
                    }

                } else if (auto download = yyjson_obj_get(target, "download")) {
                    auto url = yyjson_get_str(yyjson_obj_get(download, "url"));
                    auto type = yyjson_get_str(yyjson_obj_get(download, "type"));

                    auto tmp_file = dir.string() + ".tmp";

                    if (!fs::exists(dir) || clean) {

                        if (stage == stage_fetch) {
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
                            if (type && "zip"sv == type) {
                                std::string cmd;
                                cmd += std::format(" cd .deps && 7z x -y {0}.tmp -o{0}", name);

                                std::println("[cmd] {}", cmd);
                                std::system(cmd.c_str());
                            }

                            fs::remove(tmp_file);
                        }
                    }
                }

                if (auto cmake = yyjson_obj_get(target, "cmake")) {
                    (void)cmake;

                    auto cmake_build_dir = ".harmony-cmake-build";

                    auto profile = "RelWithDebInfo";

                    if (stage == stage_git_prepare) {
                        // TODO: Check for for successful completion of cmake configure instead of fs::exists
                        if (!fs::exists(dir / cmake_build_dir) || clean) {
                            std::string cmd;
                            cmd += std::format(" cd .deps/{}", name);
                            cmd += std::format(" && cmake . -DCMAKE_INSTALL_PREFIX={0}/install -DCMAKE_BUILD_TYPE={1} -B {0}", cmake_build_dir, profile);

                            if (auto options = yyjson_obj_get(cmake, "options")) {
                                size_t idx2, max2;
                                yyjson_val* option;
                                yyjson_arr_foreach(options, idx2, max2, option) {
                                    cmd += std::format(" -D{}", yyjson_get_str(option));
                                }
                            }

                            std::println("[cmd] {}", cmd);
                            std::system(cmd.c_str());
                        }
                    }

                    if (stage == stage_git_build) {
                        std::string cmd;
                        cmd += std::format(" cd .deps/{}", name);
                        cmd += std::format(" && cmake --build {} --config {} --target install", cmake_build_dir, profile);

                        std::println("[cmd] {}", cmd);
                        std::system(cmd.c_str());
                    }
                }
            });
        }
    }

    std::println("==== Complete ====");

    curl_global_cleanup();
}
