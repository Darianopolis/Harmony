#pragma once

#include <core.hpp>
#include <definitions.hpp>

struct Backend {
    virtual ~Backend() = 0;

    virtual void FindDependencies(std::span<const Source> sources, std::vector<std::string>& dependency_info_p1689_json) const = 0;
};

inline
Backend::~Backend() = default;
