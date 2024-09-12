#pragma once

#include <core.hpp>

enum class SourceType {
    CppSource,
    CppHeader,
    CppInterface,
};

struct Source
{
    fs::path   path;
    SourceType type;
};
