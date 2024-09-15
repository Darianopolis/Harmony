#pragma once

#include <core.hpp>
#include <configuration.hpp>
#include <build-defs.hpp>

#include <backend/backend.hpp>

// void Build(const cfg::Step& step, const Backend& backend);
void ExpandStep(const cfg::Step& step, std::vector<Task>& tasks);
void Build(std::vector<Task>& tasks, const cfg::Artifact* target, const Backend& backend);
void ParseP1689(std::string_view p1689, Task& task, std::unordered_map<fs::path, std::string>& marked_header_units);
