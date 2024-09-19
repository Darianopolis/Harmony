#pragma once

#include <core.hpp>
#include <configuration.hpp>

#ifndef HARMONY_USE_IMPORT_STD
#include <unordered_map>
#include <span>
#include <random>
#endif

struct Backend {
    virtual ~Backend() = 0;

    virtual void FindDependencies(std::span<const Task> tasks, std::vector<std::string>& dependency_info_p1689_json) const
    {
        HARMONY_IGNORE(tasks, dependency_info_p1689_json)
        Error("FindDependencies is not implemented");
    }

    virtual void GenerateStdModuleTasks(Task* std_task, Task* std_compat_task) const
    {
        HARMONY_IGNORE(std_task, std_compat_task)
        Error("GenerateStdModuleTasks is not implemented");
    }

    virtual void AddTaskInfo(std::span<Task> tasks) const
    {
        HARMONY_IGNORE(tasks)
        Error("AddTaskInfo is not implemented");
    }

    virtual bool CompileTask(const Task& task) const
    {
        HARMONY_IGNORE(task)
        Error("CompileTask is not implemented");
    }

    virtual void LinkStep(Target& target, std::span<const Task> tasks) const
    {
        HARMONY_IGNORE(target, tasks)
        Error("LinkStep is not implemented");
    }

    virtual void GenerateCompileCommands(std::span<const Task> tasks) const
    {
        // TODO: Control where output goes
        HARMONY_IGNORE(tasks)
        Error("GenerateCompileCommands is not implemented");
    };
};

inline
Backend::~Backend() = default;
