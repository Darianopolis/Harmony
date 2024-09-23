#pragma once

#include <backend/backend.hpp>

namespace msvc
{
    void EnsureVisualStudioEnvironment();
    fs::path GetVisualStudioStdModulesDir();

    void SafeCompleteCmd(std::string& cmd, const std::vector<std::string>& cmds);
    std::string PathToCmdString(const fs::path& path);

    void GenerateStdModuleTasks(Task* std_task, Task* std_compat_task);

    bool LinkStep(Target& target, std::span<const Task> tasks);
}
