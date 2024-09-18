#pragma once

#include <core.hpp>

#ifndef HARMONY_USE_STD_MODULES
#include <unordered_set>
#include <unordered_map>
#endif

enum class SourceType
{
    Unknown,

    CSource,

    CppSource,
    CppHeader,
    CppInterface,
};

struct Source
{
    fs::path path;
    SourceType type = SourceType::Unknown;
};

enum class ExecutableType
{
    Console,
    Window,
};

struct Executable {
    fs::path path;
    ExecutableType type;
};

struct Target {
    std::string name;
    std::vector<Source> sources;
    std::vector<fs::path> include_dirs;
    std::vector<fs::path> links;
    std::vector<std::string> define_build;
    std::vector<std::string> define_import;
    std::vector<std::string> import;
    std::unordered_set<Target*> flattened_imports;
    std::optional<Executable> executable;
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
    Target* target;
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

void ParseConfig(std::string_view config, std::vector<Task>& tasks, std::unordered_map<std::string, Target>& out_targets);
void Fetch(std::string_view config, bool clean);
