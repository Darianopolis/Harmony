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
    SourceType type = SourceType::Unknown;
};

struct Artifact {
    fs::path output;
};

struct Target {
    std::string name;
    std::vector<Source> sources;
    std::vector<fs::path> include_dirs;
    std::vector<std::string> define_build;
    std::vector<std::string> define_import;
    std::vector<fs::path> links;
    std::optional<Artifact> output;
    std::vector<std::string> import;
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

void ParseConfig(std::string_view config, std::vector<Task>& tasks);
void Fetch(std::string_view config, bool clean);
