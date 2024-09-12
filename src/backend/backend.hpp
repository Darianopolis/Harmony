#pragma once

#include <core.hpp>

struct Backend {
    virtual ~Backend() = 0;
};

inline
Backend::~Backend() = default;
