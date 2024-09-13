#pragma once

#include <core.hpp>

enum class SourceType {
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

struct Task {
    Source source;
    std::vector<fs::path> include_dirs;
    std::vector<std::string> defines;
    std::vector<std::string> produces;
    std::vector<std::string> depends_on;
    bool completed = false;
    bool is_header_unit = false;
};

struct BuiltTask {
    Task* task;
    fs::path bmi;
    fs::path obj;
};
