#pragma once

#ifndef HARMONY_USE_IMPORT_STD
#include <filesystem>
#include <print>
#include <string_view>
#endif

namespace fs = std::filesystem;

using namespace std::literals;

#define line_debug std::println("@@@@ - {} @ {}", __LINE__, __FILE__);

inline const fs::path BuildDir = ".harmony";

static inline bool TraceCmds = false;
inline void log_cmd(std::string_view cmd)
{
    if (TraceCmds) {
        std::println("[cmd] {}", cmd);
    }
}

inline void error(std::string_view message)
{
    std::println("[ERROR] {}", message);
    std::terminate();
}
