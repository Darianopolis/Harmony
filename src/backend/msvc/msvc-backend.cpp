#include "msvc-backend.hpp"
#include "configuration.hpp"

#include <cstdlib>
#include <filesystem>
#include <print>
#include <fstream>

MsvcBackend::~MsvcBackend() = default;

std::string PathToCmdString(const fs::path path, const fs::path& cwd = fs::current_path())
{
    // auto rel = fs::relative(path, cwd);
    // auto str = rel.string();
    (void)cwd;
    auto str = fs::absolute(path).string();
    std::ranges::transform(str, str.begin(), [](char c) {
        if (c == '/') return '\\';
        return c;
    });
    return std::format("\"{}\"", str);
}

void MsvcBackend::FindDependencies(std::span<const Source> sources, std::vector<std::string>& dependency_info_p1689_json) const
{
    fs::path output_location = BuildDir / "p1689.json";

    for (auto& source : sources) {
        auto cmd = std::format("cl.exe /std:c++latest /nologo /scanDependencies {} /TP {} ", PathToCmdString(output_location), PathToCmdString(source.path));
        std::println("[cmd] {}", cmd);
        std::system(cmd.c_str());
        {
            std::ifstream file(output_location, std::ios::binary);
            auto size = fs::file_size(output_location);
            auto& json_output = dependency_info_p1689_json.emplace_back();
            json_output.resize(size, '\0');
            file.read(json_output.data(), size);
        }
    }
}

void MsvcBackend::GenerateStdModuleTask(Task& task) const
{
    auto tools_dir = fs::path(std::getenv("VCToolsInstallDir"));
    auto module_file = tools_dir / "modules/std.ixx";

    if (!fs::exists(module_file)) {
        error("std.ixx not found. Please install the C++ Modules component for Visual Studio");
    }

    std::println("std module path: {}", module_file.string());

    task.source.path = module_file;
    task.source.type = SourceType::CppInterface;
    task.produces.emplace_back("std");
}

void MsvcBackend::CompileTask(const Task& task, fs::path* output_obj, fs::path* output_bmi, const std::unordered_map<std::string, BuiltTask>& built) const
{
    auto build_dir = BuildDir;

    auto cmd = std::format("cd {} && cl /c /nologo /std:c++latest /EHsc {}", PathToCmdString(build_dir), PathToCmdString(task.source.path, build_dir));

    if (task.is_header_unit) {
        cmd += std::format(" /exportHeader");
    }

    for (auto& depends_on : task.depends_on) {
        auto& built_dependency = built.at(depends_on);
        auto* built_task = built_dependency.task;
        if (built_task->is_header_unit) {
            cmd += std::format(" /headerUnit {}={}", PathToCmdString(built_task->source.path, build_dir), PathToCmdString(built_dependency.bmi, build_dir));
        } else {
            cmd += std::format(" /reference {}={}", depends_on, PathToCmdString(built_dependency.bmi, build_dir));
        }
    }

    auto obj = task.source.path.filename().replace_extension(".obj");
    auto bmi = task.source.path.filename().replace_extension(".ifc");

    *output_obj = build_dir / obj;
    *output_bmi = build_dir / bmi;

    cmd += std::format(" /ifcOutput {}", bmi.string());
    if (!task.is_header_unit) {
        cmd += std::format(" /Fo:{}", obj.string());
    }

    std::println("[cmd] {}", cmd);
    std::system(cmd.c_str());
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

    std::println("[cmd] {}", cmd);
    std::system(cmd.c_str());
}
