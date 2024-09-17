#pragma once

#include <core.hpp>
#include <configuration.hpp>

#include <backend/backend.hpp>

void Build(std::vector<Task>& tasks, const Artifact* target, const Backend& backend);
