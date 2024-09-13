#include "msvc-backend.hpp"
#include "configuration.hpp"

#include <cstdlib>
#include <filesystem>
#include <print>
#include <fstream>
#include <unordered_set>
#include <thread>

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

void MsvcBackend::GenerateStdModuleTask(std::vector<Task>& tasks) const
{
    auto tools_dir = fs::path(std::getenv("VCToolsInstallDir"));
    auto module_file = tools_dir / "modules/std.ixx";

    if (!fs::exists(module_file)) {
        error("std.ixx not found. Please install the C++ Modules component for Visual Studio");
    }

    std::println("std module path: {}", module_file.string());

    {
        auto& task = tasks.emplace_back();
        task.source.path = module_file;
        task.source.type = SourceType::CppInterface;
        task.produces.emplace_back("std");
    }

    {
        auto& task = tasks.emplace_back();
        task.source.path = tools_dir / "modules/std.compat.ixx";
        task.source.type = SourceType::CppInterface;
        task.produces.emplace_back("std.compat");
        task.depends_on.emplace_back("std");
    }
}

bool MsvcBackend::CompileTask(const Task& task, fs::path* output_obj, fs::path* output_bmi, const std::unordered_map<std::string, BuiltTask>& built) const
{
    auto build_dir = BuildDir;

    auto cmd = std::format("cd {} && cl /c /nologo /std:c++latest /EHsc {}", PathToCmdString(build_dir), PathToCmdString(task.source.path));

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

    // TODO: FIXME - This should be handled by shared build logic
    std::unordered_set<std::string> seen;
    auto AddDependencies = [&](this auto&& self, std::span<const std::string> dependencies) -> void {
        for (auto& depends_on : dependencies) {

            if (seen.contains(depends_on)) continue;
            seen.emplace(depends_on);

            auto& built_dependency = built.at(depends_on);
            auto* built_task = built_dependency.task;
            if (built_task->is_header_unit) {
                cmd += std::format(" /headerUnit {}={}", PathToCmdString(built_task->source.path), PathToCmdString(built_dependency.bmi));
            } else {
                cmd += std::format(" /reference {}={}", depends_on, PathToCmdString(built_dependency.bmi));
            }
            self(built_task->depends_on);
        }
    };

    AddDependencies(task.depends_on);

    // TODO: FIXME - Resolve duplicate names in BUILD, backend should be stateless!

    auto obj = fs::path(std::format("{}_{}.obj", task.source.path.filename().replace_extension("").string(), task.uid));
    auto bmi = fs::path(std::format("{}_{}.ifc", task.source.path.filename().replace_extension("").string(), task.uid));

    *output_obj = build_dir / obj;
    *output_bmi = build_dir / bmi;

    cmd += std::format(" /ifcOutput {}", bmi.string());
    if (!task.is_header_unit) {
        cmd += std::format(" /Fo:{}", obj.string());
    }

    log_cmd(cmd);

    if (!task.produces.empty() && (task.produces.front() == "std" || task.produces.front() == "std.compat")) {
        return true;
    }

    return std::system(cmd.c_str()) == EXIT_SUCCESS;
}

void MsvcBackend::LinkStep(const cfg::Artifact& artifact, const std::unordered_map<std::string, BuiltTask>& built) const
{
    auto build_dir = BuildDir;

    auto output_file = (build_dir / artifact.output).replace_extension(".exe");

    auto cmd = std::format("cd {} && link /nologo /subsystem:console /OUT:{}", PathToCmdString(build_dir), PathToCmdString(output_file));
    for (auto&[logical_name, provided] : built) {
        if (provided.task->is_header_unit) continue;
        cmd += std::format(" {}", PathToCmdString(provided.obj));
    }

    log_cmd(cmd);
}
