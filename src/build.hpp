#pragma once

#include <core.hpp>
#include <configuration.hpp>

#include <backend/backend.hpp>

void Build(std::vector<Task>& tasks, std::unordered_map<std::string, Target>& targets, const Backend& backend);

struct Component
{
    enum class Type {
        Header,
        HeaderUnit,
        Interface,
    };

    std::string name;
    Type type;
    bool exported;
    bool imported;
    bool angled;
};

struct ScanResult
{
    size_t size;
    uint64_t hash;
    std::string unique_name;
};

ScanResult ScanFile(const fs::path& path, std::string_view data, void(*callback)(void*, Component&), void* payload);

template<typename Fn>
ScanResult ScanFile(const fs::path& path, std::string_view data, Fn&& fn)
{
    return ScanFile(path, data, +[](void* payload, Component& comp) {
        static_cast<Fn *>(payload)->operator()(comp);
    }, &fn);
}
