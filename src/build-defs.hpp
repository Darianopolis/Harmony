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
    Failed,
};

struct Task;

struct Dependency {
    std::string name;
    Task* source;
};

struct Task {
    Source source;
    fs::path bmi;
    fs::path obj;
    std::string unique_name;

    std::vector<fs::path> include_dirs;
    std::vector<std::string> defines;

    std::vector<std::string> produces;
    std::vector<Dependency> depends_on;
    bool is_header_unit = false;

    TaskState state = TaskState::Waiting;

    bool external = false;
};

