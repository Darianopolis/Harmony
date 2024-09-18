#pragma once

#include "configuration.hpp"
#include <backend/backend.hpp>

struct MsvcBackend : Backend
{
    ~MsvcBackend();

    virtual void FindDependencies(std::span<const Task> tasks, std::vector<std::string>& dependency_info_p1689_json) const final override;
    virtual void GenerateStdModuleTasks(Task* std_task, Task* std_compat_task) const final override;
    virtual void AddTaskInfo(std::span<Task> tasks) const final override;
    virtual bool CompileTask(const Task& task) const final override;
    virtual void LinkStep(Target& target, std::span<const Task> tasks) const final override;
};
