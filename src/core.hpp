#pragma once

#ifndef HARMONY_USE_IMPORT_STD
#include <filesystem>
#include <print>
#include <string_view>
#include <fstream>
#endif

namespace fs = std::filesystem;

using namespace std::literals;

#define HARMONY_DEBUG_LINE std::println("@@@@ - {} @ {}", __LINE__, __FILE__);
#define HARMONY_DEBUG_EXPR(val) std::println(#val " = {}", (val));

inline const fs::path BuildDir = ".harmony";

static inline bool TraceCmds = false;
inline
void log_cmd(std::string_view cmd)
{
    if (TraceCmds) {
        std::println("[cmd] {}", cmd);
    }
}

[[noreturn]] inline
void error(std::string_view message)
{
    std::println("[ERROR] {}", message);
    std::terminate();
}

inline
std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string str;
    str.resize(fs::file_size(path));
    in.read(str.data(), str.size());
    return str;
}
