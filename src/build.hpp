#pragma once

#include <core.hpp>
#include <configuration.hpp>
#include <build-defs.hpp>

#include <backend/backend.hpp>

void ExpandStep(const Step& step, std::vector<Task>& tasks);
void Build(std::vector<Task>& tasks, const Artifact* target, const Backend& backend);
