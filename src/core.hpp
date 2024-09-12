#pragma once

#include <filesystem>
#include <print>
#include <string_view>

namespace fs = std::filesystem;

using namespace std::literals;

#define line_debug std::println("@@@@ - {} @ {}", __LINE__, __FILE__);

inline const fs::path BuildDir = ".harmony";

inline void error(std::string_view message)
{
    std::println("[ERROR] {}", message);
    std::terminate();
}
