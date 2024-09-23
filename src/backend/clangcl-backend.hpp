#pragma once

#include <core.hpp>

#include <backend/backend.hpp>

struct ClangClBackend : Backend
{
    ClangClBackend();
    ~ClangClBackend() final;

    void FindDependencies(const Task& task, std::string& dependency_info_p1689_json) const final;
    void GenerateStdModuleTasks(Task* std_task, Task* std_compat_task) const final;
    void AddTaskInfo(std::span<Task> tasks) const final;
    bool CompileTask(const Task& task) const final;
    void GenerateCompileCommands(std::span<const Task> tasks) const final;
    bool LinkStep(Target& target, std::span<const Task> tasks) const final;
};
