#pragma once

#include <core.hpp>

#ifndef HARMONY_USE_IMPORT_STD
#include <unordered_set>
#include <unordered_map>
#endif

struct Backend;

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
    SourceType detected_type = SourceType::Unknown;
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

struct Git
{
    std::string url;
    std::optional<std::string> branch;
    std::vector<std::string> options;
};

enum class ArchiveType
{
    None,
    Zip,
};

struct Download
{
    std::string url;
    ArchiveType type = ArchiveType::None;
};

struct CMake
{
    std::vector<std::string> options;
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

    // TODO: This should just be a list of type erased "plugin" configuration
    std::optional<Git> git;
    std::optional<Download> download;
    std::optional<CMake> cmake;
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

struct BuildState
{
    std::vector<Task> tasks;
    std::unordered_map<std::string, Target> targets;
    const Backend* backend;
};

void ParseTargetsFile(BuildState& state, std::string_view config);
void FetchExternalData(BuildState& state, bool clean, bool update);
void ExpandTargets(BuildState& state);
void ScanDependencies(BuildState& state, bool use_backend_dependency_scan);
void DetectAndInsertStdModules(BuildState& state);
void Build(BuildState&);

struct Component
{
    enum class Type {
        Header,
        HeaderUnit,
        Interface,
    };

    std::string name;
    Type type;
    bool exported;
    bool imported;
    bool angled;
};

struct ScanResult
{
    size_t size;
    uint64_t hash;
    std::string unique_name;
};

ScanResult ScanFile(const fs::path& path, std::string& storage, FunctionRef<void(Component&)>);
