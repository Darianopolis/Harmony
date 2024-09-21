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

        auto tools_dir = GetVisualStudioStdModulesDir();
        auto module_file = tools_dir / "std.ixx";

        LogDebug("Modules file path: {}", module_file.string());

        if (!fs::exists(module_file)) {
            Error("std.ixx not found. Please install the C++ Modules component for Visual Studio");
        }

        LogDebug("std module path: {}", module_file.string());

        if (std_task) {
            std_task->source.path = module_file;
        }

        if (std_compat_task) {
            std_compat_task->source.path = tools_dir / "std.compat.ixx";
        }
    }
}
