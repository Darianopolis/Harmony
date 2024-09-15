#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include "msvc-backend.hpp"
#include "configuration.hpp"

#ifndef HARMONY_USE_IMPORT_STD
#include <cstdlib>
#include <filesystem>
#include <print>
#include <fstream>
#include <unordered_set>
#include <thread>
#endif

MsvcBackend::~MsvcBackend() = default;

std::string PathToCmdString(const fs::path path)
{
    auto str = fs::absolute(path).string();
    std::ranges::transform(str, str.begin(), [](char c) {
        if (c == '/') return '\\';
        return c;
    });
    return std::format("\"{}\"", str);
}

void MsvcBackend::FindDependencies(std::span<const Task> tasks, std::vector<std::string>& dependency_info_p1689_json) const
{
    dependency_info_p1689_json.resize(tasks.size());

    // fs::path output_location = BuildDir / "p1689.json";
    // for (auto& task : tasks) {

#pragma omp parallel for
    for (uint32_t i = 0; i < tasks.size(); ++i) {
        auto& task = tasks[i];
        auto output_location = BuildDir / std::format("p1689_{}.json", i);

        auto cmd = std::format("cl.exe /std:c++latest /nologo /scanDependencies {} /TP {} ", PathToCmdString(output_location), PathToCmdString(task.source.path));

        for (auto& include_dir : task.include_dirs) {
            cmd += std::format(" /I{}", PathToCmdString(include_dir));
        }

        for (auto& define : task.defines) {
            cmd += std::format(" /D{}", define);
        }

        log_cmd(cmd);
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

void MsvcBackend::GenerateStdModuleTasks(Task* std_task, Task* std_compat_task) const
{
    auto tools_dir = fs::path(std::getenv("VCToolsInstallDir"));
    auto module_file = tools_dir / "modules/std.ixx";

    if (!fs::exists(module_file)) {
        error("std.ixx not found. Please install the C++ Modules component for Visual Studio");
    }

    std::println("std module path: {}", module_file.string());

    if (std_task) {
        std_task->source.path = module_file;
    }

    if (std_compat_task) {
        std_compat_task->source.path = tools_dir / "modules/std.compat.ixx";
    }
}

void MsvcBackend::AddTaskInfo(std::span<Task> tasks) const
{
    auto build_dir = BuildDir;

    for (auto& task : tasks) {
        auto obj = fs::path(std::format("{}.obj", task.unique_name));
        auto bmi = fs::path(std::format("{}.ifc", task.unique_name));

        task.obj = build_dir / obj;
        task.bmi = build_dir / bmi;
    }
}

bool MsvcBackend::CompileTask(const Task& task) const
{
    auto build_dir = BuildDir;

    auto cmd = std::format("cd /d {} && cl /c /nologo /std:c++latest /EHsc {}", PathToCmdString(build_dir), PathToCmdString(task.source.path));
    // auto cmd = std::format("cl /c /nologo /std:c++latest /EHsc {}", PathToCmdString(task.source.path));

    cmd += " /Zc:preprocessor /utf-8 /DUNICODE /D_UNICODE /permissive- /Zc:__cplusplus";
    // cmd += " /O2 /Ob3";

    for (auto& include_dir : task.include_dirs) {
        cmd += std::format(" /I{}", PathToCmdString(include_dir));
    }

    for (auto& define : task.defines) {
        cmd += std::format(" /D{}", define);
    }

    if (task.is_header_unit) {
        cmd += std::format(" /exportHeader");
    }

    // TODO: FIXME - Should this be handled by shared build logic?
    std::unordered_set<std::string_view> seen;
    auto AddDependencies = [&cmd, &seen](this auto&& self, const Task& task) -> void {
        for (auto& depends_on : task.depends_on) {

            if (seen.contains(depends_on.name)) continue;
            seen.emplace(depends_on.name);

            if (depends_on.source->is_header_unit) {
                cmd += std::format(" /headerUnit {}={}", PathToCmdString(depends_on.source->source.path), PathToCmdString(depends_on.source->bmi));
            } else {
                cmd += std::format(" /reference {}={}", depends_on.name, PathToCmdString(depends_on.source->bmi));
            }
            self(*depends_on.source);
        }
    };

    AddDependencies(task);

    cmd += std::format(" /ifcOutput {}", task.bmi.filename().string());
    if (!task.is_header_unit) {
        cmd += std::format(" /Fo:{}", task.obj.filename().string());
    }

    log_cmd(cmd);

    return std::system(cmd.c_str()) == 0;
}

void MsvcBackend::LinkStep(const cfg::Artifact& artifact, std::span<const Task> tasks) const
{
    auto build_dir = BuildDir;

    auto output_file = (build_dir / artifact.output).replace_extension(".exe");

    auto cmd = std::format("cd /d {} && link /nologo /subsystem:console /OUT:{}", PathToCmdString(build_dir), PathToCmdString(output_file));
    // auto cmd = std::format("link /nologo /subsystem:console /OUT:{}", PathToCmdString(output_file));
    for (auto& task : tasks) {
        if (task.is_header_unit) continue;
        cmd += std::format(" {}", PathToCmdString(task.obj));
    }

    log_cmd(cmd);
}
