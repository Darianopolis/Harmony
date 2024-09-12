#pragma once

#include <core.hpp>
#include <configuration.hpp>
#include <build-defs.hpp>

#include <backend/backend.hpp>

void Build(const cfg::Step& step, const Backend& backend);
