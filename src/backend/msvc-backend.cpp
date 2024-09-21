#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include "msvc-backend.hpp"
#include "msvc-common.hpp"
#include "configuration.hpp"

#ifndef HARMONY_USE_IMPORT_STD
#include <cstdlib>
#include <filesystem>
#include <print>
#include <fstream>
#include <unordered_set>
#endif

MsvcBackend::MsvcBackend()
{
    msvc::EnsureVisualStudioEnvironment();
}

MsvcBackend::~MsvcBackend() = default;

void MsvcBackend::FindDependencies(const Task& task, std::string& dependency_info_p1689_json) const
{
    auto output_location = BuildDir / std::format("{}.p1689.json", task.unique_name);

    auto cmd = std::format("cl.exe /std:c++latest /nologo /scanDependencies {} /TP {} ", msvc::PathToCmdString(output_location), msvc::PathToCmdString(task.source.path));

    for (auto& include_dir : task.include_dirs) {
        cmd += std::format(" /I{}", msvc::PathToCmdString(include_dir));
    }

    for (auto& define : task.defines) {
        cmd += std::format(" /D{}", define);
    }

    LogCmd(cmd);
    std::system(cmd.c_str());
    {
        std::ifstream file(output_location, std::ios::binary);
        auto size = fs::file_size(output_location);
        dependency_info_p1689_json.resize(size, '\0');
        file.read(dependency_info_p1689_json.data(), size);
    }
}

void MsvcBackend::GenerateStdModuleTasks(Task* std_task, Task* std_compat_task) const
{
    msvc::GenerateStdModuleTasks(std_task, std_compat_task);
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

    auto cmd = std::format("cd /d {} && cl", msvc::PathToCmdString(build_dir));

    std::vector<std::string> cmds;

    cmds.emplace_back(std::format("/c /nologo /std:c++latest /EHsc"));
    switch (task.source.type) {
        break;case SourceType::CSource: cmds.emplace_back(std::format("/TC {}", msvc::PathToCmdString(task.source.path)));
        break;case SourceType::CppSource: cmds.emplace_back(std::format("/TP {}", msvc::PathToCmdString(task.source.path)));
        break;case SourceType::CppHeader: {
            if (task.is_header_unit) {
                cmds.emplace_back(std::format("/exportHeader /TP {}", msvc::PathToCmdString(task.source.path)));
            } else Error("Attempted to compile header that isn't being exported as a header unit");
        }
        break;case SourceType::CppInterface: cmds.emplace_back(std::format("/interface /TP {}", msvc::PathToCmdString(task.source.path)));
        break;default: Error("Cannot compile: unknown source type!");
    }

    // cmd += " /Zc:preprocessor /utf-8 /DUNICODE /D_UNICODE /permissive- /Zc:__cplusplus";
    cmds.emplace_back("/Zc:preprocessor /permissive-");
    cmds.emplace_back("/DWIN32 /D_WINDOWS /EHsc /Ob0 /Od /RTC1 -std:c++latest -MD");
    // cmd += " /O2 /Ob3";
    // cmd += " /W4";

    // cmds.emplace_back("/FORCE /IGNORE:4006"); // When linking results from clang-cl that consume import std;

    for (auto& include_dir : task.include_dirs) {
        cmds.emplace_back(std::format("/I{}", msvc::PathToCmdString(include_dir)));
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
                cmds.emplace_back(std::format("/headerUnit {}={}", msvc::PathToCmdString(depends_on.source->source.path), msvc::PathToCmdString(depends_on.source->bmi)));
            } else {
                cmds.emplace_back(std::format("/reference {}={}", depends_on.name, msvc::PathToCmdString(depends_on.source->bmi)));
            }
            self(*depends_on.source);
        }
    };

    AddDependencies(task);

    cmds.emplace_back(std::format("/ifcOutput {}", task.bmi.filename().string()));
    if (!task.is_header_unit) {
        cmds.emplace_back(std::format("/Fo:{}", task.obj.filename().string()));
    }

    // Generate cmd string, use command files to avoid cmd line size limit

    msvc::SafeCompleteCmd(cmd, cmds);

    LogCmd(cmd);

    return std::system(cmd.c_str()) == 0;
}

void MsvcBackend::LinkStep(Target& target, std::span<const Task> tasks) const
{
    auto build_dir = BuildDir;

    auto& executable = target.executable.value();

    auto output_file = (executable.path).replace_extension(".exe");
    fs::create_directories(output_file.parent_path());

    // auto cmd = std::format("cd /d {} && link /nologo /subsystem:console /OUT:{}", msvc::PathToCmdString(build_dir), msvc::PathToCmdString(output_file));
    auto cmd = std::format("cd /d {} && link /nologo", msvc::PathToCmdString(output_file.parent_path()));
    std::vector<std::string> cmds;
    switch (executable.type) {
        break;case ExecutableType::Console: cmds.emplace_back("/subsystem:console");
        break;case ExecutableType::Window: cmds.emplace_back("/subsystem:window");
    }
    cmds.emplace_back(std::format("/OUT:{}", output_file.filename().string()));
    for (auto& task : tasks) {
        if (task.is_header_unit) continue;
        if (!target.flattened_imports.contains(task.target)) continue;
        cmds.emplace_back(msvc::PathToCmdString(task.obj));
    }

    cmds.emplace_back("user32.lib");
    cmds.emplace_back("gdi32.lib");
    cmds.emplace_back("shell32.lib");
    cmds.emplace_back("Winmm.lib");
    cmds.emplace_back("Advapi32.lib");
    cmds.emplace_back("Comdlg32.lib");
    cmds.emplace_back("comsuppw.lib");
    cmds.emplace_back("onecore.lib");

    // TODO: Move this to shared logic!
    auto AddLinks = [&](const Target& t) {
        LogTrace("Adding links for: [{}]", t.name);
        for (auto& link : t.links) {
            if (fs::is_regular_file(link)) {
                LogTrace("    adding: [{}]", link.string());
                cmds.emplace_back(msvc::PathToCmdString(link));
            } else if (fs::is_directory(link)) {
                LogTrace("  finding links in: [{}]", link.string());
                for (auto iter : fs::directory_iterator(link)) {
                    auto path = iter.path();
                    if (path.extension() == ".lib") {
                        LogTrace("    adding: [{}]", path.string());
                        cmds.emplace_back(msvc::PathToCmdString(path));
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

    msvc::SafeCompleteCmd(cmd, cmds);

    LogCmd(cmd);

    std::system(cmd.c_str());
}