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

    virtual void FindDependencies(std::span<const Task> tasks, std::vector<std::string>& dependency_info_p1689_json) const = 0;
    virtual void GenerateStdModuleTasks(Task* std_task, Task* std_compat_task) const = 0;
    virtual void AddTaskInfo(std::span<Task> tasks) const = 0;
    virtual bool CompileTask(const Task& task) const = 0;
    virtual void LinkStep(Target& target, std::span<const Task> tasks) const = 0;
};

inline
Backend::~Backend() = default;
