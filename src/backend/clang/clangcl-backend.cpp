#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include "clangcl-backend.hpp"
#include "configuration.hpp"

#ifndef HARMONY_USE_IMPORT_STD
#include <cstdlib>
#include <filesystem>
#include <print>
#include <fstream>
#include <unordered_set>
#include <thread>
#endif

// TODO: Abstract json writing
#include <yyjson.h>

constexpr std::string_view ClangScanDepsPath = "D:/Dev/Cloned-Temp/llvm-project/build/bin/clang-scan-deps.exe";
constexpr std::string_view ClangClPath = "D:/Dev/Cloned-Temp/llvm-project/build/bin/clang-cl.exe";

ClangClBackend::~ClangClBackend() = default;

static
auto PathToCmdString(const fs::path& path)
{
    return FormatPath(path, PathFormatOptions::Backward | PathFormatOptions::QuoteSpaces | PathFormatOptions::Absolute);
}

void ClangClBackend::FindDependencies(std::span<const Task> tasks, std::vector<std::string>& dependency_info_p1689_json) const
{
    dependency_info_p1689_json.resize(tasks.size());

    // fs::path output_location = BuildDir / "p1689.json";
    // for (auto& task : tasks) {

#pragma omp parallel for
    for (uint32_t i = 0; i < tasks.size(); ++i) {
        auto& task = tasks[i];
        auto output_location = BuildDir / std::format("p1689_{}.json", i);

        auto cmd = std::format("{} -format=p1689 -o {} -- {} /std:c++latest /nologo -x c++-module {} ",
            ClangScanDepsPath, PathToCmdString(output_location), ClangClPath, PathToCmdString(task.source.path));

        for (auto& include_dir : task.include_dirs) {
            cmd += std::format(" /I{}", PathToCmdString(include_dir));
        }

        for (auto& define : task.defines) {
            cmd += std::format(" /D{}", define);
        }

        LogCmd(cmd);
        std::system(cmd.c_str());
        {
            std::ifstream file(output_location, std::ios::binary);
            auto size = fs::file_size(output_location);
            // auto& json_output = dependency_info_p1689_json.emplace_back();
            auto& json_output = dependency_info_p1689_json[i];
            json_output.resize(size, '\0');
            file.read(json_output.data(), size);
        }
    }
}

void ClangClBackend::GenerateStdModuleTasks(Task* std_task, Task* std_compat_task) const
{
    auto tools_dir = fs::path(std::getenv("VCToolsInstallDir"));
    auto module_file = tools_dir / "modules/std.ixx";

    if (!fs::exists(module_file)) {
        Error("std.ixx not found. Please install the C++ Modules component for Visual Studio");
    }

    std::println("std module path: {}", module_file.string());

    if (std_task) {
        std_task->source.path = module_file;
    }

    if (std_compat_task) {
        std_compat_task->source.path = tools_dir / "modules/std.compat.ixx";
    }
}

void ClangClBackend::AddTaskInfo(std::span<Task> tasks) const
{
    auto build_dir = BuildDir;

    for (auto& task : tasks) {
        auto obj = fs::path(std::format("{}.obj", task.unique_name));
        auto bmi = fs::path(std::format("{}.pcm", task.unique_name));

        task.obj = build_dir / obj;
        task.bmi = build_dir / bmi;
    }
}

static
void SafeCompleteCmd(std::string& cmd, std::vector<std::string>& cmds)
{
    auto build_dir = BuildDir;

    size_t cmd_length = cmd.size();
    for (auto& cmd_part : cmds) {
        cmd_length += 1 + cmd_part.size();
    }

    constexpr uint32_t CmdSizeLimit = 4000;

    if (cmd_length > CmdSizeLimit) {
        static std::atomic_uint32_t cmd_file_id = 0;

        auto cmd_dir = build_dir / "cmds";
        fs::create_directories(cmd_dir);
        auto cmd_path = cmd_dir / std::format("cmd.{}", cmd_file_id++);
        std::ofstream out(cmd_path, std::ios::binary);
        for (uint32_t i = 0; i < cmds.size(); ++i) {
            if (i > 0) out.write("\n", 1);
            out.write(cmds[i].data(), cmds[i].size());
        }
        out.flush();
        out.close();

        cmd += " @";
        cmd += PathToCmdString(cmd_path);

    } else {
        for (auto& cmd_part : cmds) {
            cmd += " ";
            cmd += cmd_part;
        }
    }
}

bool ClangClBackend::CompileTask(const Task& task) const
{
    auto build_dir = BuildDir;

    auto cmd = std::format("cd /d {} && {}", PathToCmdString(build_dir), ClangClPath);

    std::vector<std::string> cmds;

    cmds.emplace_back(std::format("/c /nologo -Wno-everything /EHsc"));
    switch (task.source.type) {
        break;case SourceType::CSource: cmds.emplace_back(std::format("-x c {}", PathToCmdString(task.source.path)));
        break;case SourceType::CppSource: cmds.emplace_back(std::format("/std:c++latest -x c++ {}", PathToCmdString(task.source.path)));
        break;case SourceType::CppHeader: {
            if (task.is_header_unit) {
                cmds.emplace_back(std::format("/std:c++latest -x c++ -fmodule-header={}", PathToCmdString(task.source.path)));
            } else Error("Attempted to compile header that isn't being exported as a header unit");
        }
        break;case SourceType::CppInterface: cmds.emplace_back(std::format("/std:c++latest -x c++-module {}", PathToCmdString(task.source.path)));
        break;default: Error("Cannot compile: unknown source type!");
    }

    // cmd += " /Zc:preprocessor /utf-8 /DUNICODE /D_UNICODE /permissive- /Zc:__cplusplus";
    // cmds.emplace_back("/Zc:preprocessor /permissive-");
    // cmds.emplace_back("/DWIN32 /D_WINDOWS /EHsc /Ob0 /Od /RTC1 /std:c++latest -MD");

    for (auto& include_dir : task.include_dirs) {
        cmds.emplace_back(std::format("/I{}", PathToCmdString(include_dir)));
    }

    for (auto& define : task.defines) {
        cmds.emplace_back(std::format("/D{}", define));
    }

    // TODO: FIXME - Should this be handled by shared build logic?
    std::unordered_set<std::string_view> seen;
    auto AddDependencies = [&cmds, &seen](this auto&& self, const Task& task) -> void {
        for (auto& depends_on : task.depends_on) {

            if (seen.contains(depends_on.name)) continue;
            seen.emplace(depends_on.name);

            if (depends_on.source->is_header_unit) {
                cmds.emplace_back(std::format("-fmodule-file={}", PathToCmdString(depends_on.source->source.path), PathToCmdString(depends_on.source->bmi)));
            } else {
                cmds.emplace_back(std::format("-fmodule-file={}={}", depends_on.name, PathToCmdString(depends_on.source->bmi)));
            }
            self(*depends_on.source);
        }
    };

    AddDependencies(task);

    if (task.source.type == SourceType::CppInterface || task.is_header_unit) {
        cmds.emplace_back(std::format("-fmodule-output={}", task.bmi.filename().string()));
    }
    if (!task.is_header_unit) {
        cmds.emplace_back(std::format("-o {}", task.obj.filename().string()));
    }

    // Generate cmd string, use command files to avoid cmd line size limit

    SafeCompleteCmd(cmd, cmds);

    LogCmd(cmd);

    return std::system(cmd.c_str()) == 0;
}

void ClangClBackend::GenerateCompileCommands(std::span<const Task> tasks) const
{
    auto build_dir = BuildDir;

    auto doc = yyjson_mut_doc_new(nullptr);

    auto root = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);

    for (auto& task : tasks) {
        auto out_task = yyjson_mut_arr_add_obj(doc, root);

        // Deduplicate
        auto cmd = std::format("{}", ClangClPath);

        std::vector<std::string> cmds;

        cmds.emplace_back(std::format("/c /nologo -Wno-everything /EHsc"));
        switch (task.source.type) {
            break;case SourceType::CSource: cmds.emplace_back(std::format("-x c {}", PathToCmdString(task.source.path)));
            break;case SourceType::CppSource: cmds.emplace_back(std::format("/std:c++latest -x c++ {}", PathToCmdString(task.source.path)));
            break;case SourceType::CppHeader: {
                if (task.is_header_unit) {
                    cmds.emplace_back(std::format("/std:c++latest -x c++ -fmodule-header={}", PathToCmdString(task.source.path)));
                } else Error("Attempted to compile header that isn't being exported as a header unit");
            }
            break;case SourceType::CppInterface: cmds.emplace_back(std::format("/std:c++latest -x c++-module {}", PathToCmdString(task.source.path)));
            break;default: Error("Cannot compile: unknown source type!");
        }

        // cmd += " /Zc:preprocessor /utf-8 /DUNICODE /D_UNICODE /permissive- /Zc:__cplusplus";
        // cmds.emplace_back("/Zc:preprocessor /permissive-");
        // cmds.emplace_back("/DWIN32 /D_WINDOWS /EHsc /Ob0 /Od /RTC1 /std:c++latest -MD");

        for (auto& include_dir : task.include_dirs) {
            cmds.emplace_back(std::format("/I{}", PathToCmdString(include_dir)));
        }

        for (auto& define : task.defines) {
            cmds.emplace_back(std::format("/D{}", define));
        }

        if (task.source.type == SourceType::CppInterface || task.is_header_unit) {
            cmds.emplace_back(std::format("-fmodule-output={}", task.bmi.filename().string()));
        }
        if (!task.is_header_unit) {
            cmds.emplace_back(std::format("-o {}", task.obj.filename().string()));
        }

        SafeCompleteCmd(cmd, cmds);

        yyjson_mut_obj_add_strcpy(doc, out_task, "directory", fs::absolute(build_dir).string().c_str());
        yyjson_mut_obj_add_strcpy(doc, out_task, "command", cmd.c_str());
        yyjson_mut_obj_add_strcpy(doc, out_task, "file", fs::absolute(task.source.path).string().c_str());
        yyjson_mut_obj_add_strcpy(doc, out_task, "output", fs::absolute(task.obj).string().c_str());
    }

    {
        yyjson_write_flag flags = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
        yyjson_write_err err;
        yyjson_mut_write_file("compile_commands.json", doc, flags, nullptr, &err);
        if (err.code) {
            std::println("JSON write error ({}): {}", err.code, err.msg);
        }
    }
}
