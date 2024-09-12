#include "msvc-backend.hpp"

MsvcBackend::~MsvcBackend() = default;

std::string PathToCmdString(const fs::path path)
{
    auto rel = fs::relative(path, fs::current_path());
    auto str = rel.string();
    std::ranges::transform(str, str.begin(), [](char c) {
        if (c == '/') return '\\';
        return c;
    });
    return std::format("\"{}\"", str);
}

void MsvcBackend::FindDependencies(std::span<const Source> sources, std::vector<std::string>& dependency_info_p1689_json) const
{
    fs::path output_location = "out/p1689.json";

    for (auto& source : sources) {
        auto cmd = std::format("cl.exe /std:c++latest /nologo /scanDependencies {} /TP {} ", PathToCmdString(output_location), PathToCmdString(source.path));
        std::println("[cmd] {}", cmd);
        std::system(cmd.c_str());
        {
            std::ifstream file(output_location, std::ios::binary);
            auto size = fs::file_size(output_location);
            auto& json_output = dependency_info_p1689_json.emplace_back();
            json_output.resize(size, '\0');
            file.read(json_output.data(), size);
        }
    }

    fs::remove(output_location);
}
