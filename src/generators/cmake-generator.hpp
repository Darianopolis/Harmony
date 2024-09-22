#pragma once

#include "build.hpp"

void GenerateCMake(const fs::path& output_dir, std::vector<Task>& tasks, std::unordered_map<std::string, Target>& targets);
