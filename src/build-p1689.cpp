#include "build.hpp"

// #include <json/json.hpp>

#pragma warning(push)
#pragma warning(disable: 4100)
#include <simdjson.h>
#pragma warning(pop)

void ParseP1689(std::string_view p1689, Task& task, std::unordered_map<fs::path, std::string>& marked_header_units)
{
    // json::buffer json(dependency_info[i]);
    // json.tokenize();
    // auto rule = json.begin()["rules"].begin();

    // for (auto provided : rule["provides"]) {
    //         auto logical_name = provided["logical-name"].string();
    //         std::println("  provides: {}", logical_name);
    //         task.produces.emplace_back(logical_name);
    // }

    // for (auto required : rule["requires"]) {
    //     auto logical_name = required["logical-name"].string();
    //     std::println("  requires: {}", logical_name);
    //     task.depends_on.emplace_back(logical_name);
    //     if (required["source-path"]) {
    //         auto path = fs::path(required["source-path"].string());
    //         std::println("    is header unit - {}", path.string());
    //         marked_header_units[path] = logical_name;
    //     }
    // }

    using namespace simdjson;
    ondemand::parser parser;
    padded_string json_str(p1689);
    auto doc = parser.iterate(json_str);

    try {
        auto rule = *doc["rules"].get_array().value().begin();
        if (!rule["provides"].error()) {
            for (auto provided : rule["provides"].get_array().value()) {
                    auto logical_name = provided["logical-name"].get_string().value();
                    std::println("  provides: {}", logical_name);
                    task.produces.emplace_back(logical_name);
            }
        }

        if (!rule["requires"].error()) {
            for (auto required : rule["requires"].get_array().value()) {
                auto logical_name = required["logical-name"].get_string().value();
                std::println("  requires: {}", logical_name);
                task.depends_on.emplace_back(logical_name);
                if (!required["source-path"].error()) {
                    auto path = fs::path(required["source-path"].get_string().value());
                    std::println("    is header unit - {}", path.string());
                    marked_header_units[path] = logical_name;
                }
            }
        }
    } catch (const std::exception& e) {
        error(e.what());
    } catch (...) {
        error("Unknown Error!");

    }
}
