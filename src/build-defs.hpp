#pragma once

#include <core.hpp>

enum class SourceType
{
    Unknown,

    CppSource,
    CppHeader,
    CppInterface,
};

struct Source
{
    fs::path path;
    SourceType type;
};

enum class TaskState
{
    Waiting,
    Compiling,
    Complete,
};

struct Task {
    Source source;
    std::vector<fs::path> include_dirs;
    std::vector<std::string> defines;
    std::vector<std::string> produces;
    std::vector<std::string> depends_on;
    TaskState state = TaskState::Waiting;
    bool is_header_unit = false;

    static std::atomic_uint32_t next_uid;
    uint32_t uid = next_uid++;
};

inline std::atomic_uint32_t Task::next_uid = 0;

struct BuiltTask {
    Task* task;
    fs::path bmi;
    fs::path obj;
    bool success = false;
};
