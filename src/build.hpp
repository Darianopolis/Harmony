#pragma once

#include <core.hpp>
#include <configuration.hpp>

#include <backend/backend.hpp>

void Build(std::vector<Task>& tasks, std::unordered_map<std::string, Target> targets, const Backend& backend);
