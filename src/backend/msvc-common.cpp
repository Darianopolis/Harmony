#define NOMINMAX
#include <Windows.h>

#include "msvc-common.hpp"

static const fs::path VisualStudioEnvPath = BuildDir / "env";
static constexpr const char* VCToolsInstallDirEnvName = "VCToolsInstallDir";

namespace win32
{
    // TODO: Move this into a separate win32 helper?
    std::optional<std::string> GetEnv(const char* name)
    {
        char value[32'767];
        auto len = GetEnvironmentVariableA(name, value, sizeof(value));
        if (len) return std::string(value, len);
        return std::nullopt;
    }
}

namespace msvc
{
    void SetupVisualStudioEnvironment()
    {
        if (win32::GetEnv(VCToolsInstallDirEnvName)) {
            LogDebug("Using existing Visual Studio environment");
            return;
        }

        if (!fs::exists(VisualStudioEnvPath)) {
            fs::create_directories(VisualStudioEnvPath.parent_path());
            LogDebug("Generating Visual Studio environment in [{}]", VisualStudioEnvPath.string());
            constexpr std::string_view VcvarsPath = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat";
            std::system(std::format("\"{}\" && set > {}", VcvarsPath, VisualStudioEnvPath.string()).c_str());
        } else {
            LogDebug("Loading Visual Studio environment from [{}]", VisualStudioEnvPath.string());
        }

        std::ifstream in(VisualStudioEnvPath, std::ios::binary);
        if (!in.is_open()) {
            Error("Could not read environment file!");
        }

        std::string line;
        while (std::getline(in, line)) {
            auto equals = line.find_first_of("=");
            if (equals == std::string::npos) {
                Error(std::format("Invalid comand syntex on line:\n{}", line));
            }
            line[equals] = '\0';
            std::ranges::transform(line, line.data(), [](const char c) {
                // Handle CRLF
                return c == '\r' ? '\0' : c;
            });
            LogTrace("Setting env[{}] = {}", line.c_str(), line.c_str() + equals + 1);
            SetEnvironmentVariableA(line.c_str(), line.c_str() + equals + 1);
        }
    }

    void EnsureVisualStudioEnvironment()
    {
        HARMONY_DO_ONCE()
        {
            SetupVisualStudioEnvironment();
        };
    }

    fs::path GetVisualStudioStdModulesDir()
    {
        auto vctoolsdir = win32::GetEnv(VCToolsInstallDirEnvName);
        if (!vctoolsdir) {
            Error("Not running in a valid VS developer environment");
        }
        return fs::path(*vctoolsdir) / "modules";
    }
}

// ---------------------------------------------------------------------------------------------------------------------

namespace msvc
{
    std::string PathToCmdString(const fs::path& path)
    {
        return FormatPath(path, PathFormatOptions::Backward | PathFormatOptions::QuoteSpaces | PathFormatOptions::Absolute);
    }

    void SafeCompleteCmd(std::string& cmd, const std::vector<std::string>& cmds)
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
}

// ---------------------------------------------------------------------------------------------------------------------

namespace msvc
{
    void GenerateStdModuleTasks(Task* std_task, Task* std_compat_task)
    {
        EnsureVisualStudioEnvironment();

        auto modules_dir = GetVisualStudioStdModulesDir();

        if (!fs::exists(modules_dir)) {
            Error("VS modules dir not found. Please install the C++ Modules component for Visual Studio");
        }

        LogTrace("Found std modules in [{}]", modules_dir.string());

        if (std_task) {
            std_task->source.path = modules_dir / "std.ixx";
        }

        if (std_compat_task) {
            std_compat_task->source.path = modules_dir / "std.compat.ixx";
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------

namespace msvc
{
    bool LinkStep(Target& target, std::span<const Task> tasks)
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
            if (!target.flattened_imports.contains(task.target) && &target != task.target) continue;
            if (!fs::exists(task.obj)) {
                LogWarn("Could not find obj for [{}]", task.unique_name);
            }
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

        msvc::ForEachLink(target, [&](auto& link) {
            cmds.emplace_back(msvc::PathToCmdString(link));
        });

        msvc::SafeCompleteCmd(cmd, cmds);

        LogCmd(cmd);

        return std::system(cmd.c_str()) == 0;
    }

    void AddSystemIncludeDirs(BuildState& state)
    {
        auto _includes = win32::GetEnv("INCLUDE");
        if (!_includes) Error("Not in valid VS enviornment, missing 'INCLUDE' environment variable");
        auto& includes = *_includes;

        auto start = 0;
        while (start < includes.size()) {
            auto sep = includes.find_first_of(';', start);
            auto end = std::min(sep, includes.size());
            auto include = includes.substr(start, end - start);
            LogTrace("Found system include dir: [{}]", include);
            state.system_includes.emplace_back(std::move(include));
            start = end + 1;
        }
    }

    void ForEachLink(const Target& target, FunctionRef<void(const fs::path&)> callback)
    {
        // TODO: Move this to shared logic!
        // TODO: Backend should only care about identifying .lib extensions
        auto AddLinks = [&](const Target& t) {
            LogTrace("Adding links for: [{}]", t.name);
            for (auto& link : t.links) {
                if (fs::is_regular_file(link)) {
                    LogTrace("    adding: [{}]", link.string());
                    callback(link);
                } else if (fs::is_directory(link)) {
                    LogTrace("  finding links in: [{}]", link.string());
                    for (auto iter : fs::directory_iterator(link)) {
                        auto path = iter.path();
                        if (path.extension() == ".lib") {
                            LogTrace("    adding: [{}]", path.string());
                            callback(path);
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
    }

    void ForEachShared(const Target& target, FunctionRef<void(const fs::path&)> callback)
    {
        // TODO: Move this to shared logic!
        // TODO: Backend should only care about identifying .dll extensions
        auto AddShared = [&](const Target& t) {
            LogTrace("Adding shared libraries for: [{}]", t.name);
            for (auto& shared : t.shared) {
                if (fs::is_regular_file(shared)) {
                    LogTrace("    adding: [{}]", shared.string());
                    callback(shared);
                } else if (fs::is_directory(shared)) {
                    LogTrace("  finding shared libraries in: [{}]", shared.string());
                    for (auto iter : fs::directory_iterator(shared)) {
                        auto path = iter.path();
                        if (path.extension() == ".dll") {
                            LogTrace("    adding: [{}]", path.string());
                            callback(path);
                        }
                    }
                } else {
                    LogWarn("shared library path not found: [{}]", shared.string());
                }
            }
        };
        AddShared(target);
        for (auto* import_target : target.flattened_imports) {
            AddShared(*import_target);
        }
    }
}
