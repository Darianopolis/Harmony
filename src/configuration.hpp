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

struct Step {
    std::vector<Source> sources;
    std::vector<fs::path> include_dirs;
    std::vector<std::string> defines;
    std::optional<Artifact> output;
};
