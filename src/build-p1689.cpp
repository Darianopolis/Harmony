#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include "build.hpp"

#include "yyjson.h"

void ParseP1689(std::string_view p1689, Task& task, std::unordered_map<fs::path, std::string>& marked_header_units)
{
    auto* doc = yyjson_read(p1689.data(), p1689.size(), 0);
    auto* root = yyjson_doc_get_root(doc);

    auto rule = yyjson_arr_get_first(yyjson_obj_get(root, "rules"));

    if (auto provided_list = yyjson_obj_get(rule, "provides")) {
        size_t idx, max;
        yyjson_val* provided;
        yyjson_arr_foreach(provided_list, idx, max, provided) {
            auto logical_name = yyjson_get_str(yyjson_obj_get(provided, "logical-name"));
            std::println("  provides: {}", logical_name);
            task.produces.emplace_back(logical_name);
        }
    }

    if (auto required_list = yyjson_obj_get(rule, "requires")) {
        size_t idx, max;
        yyjson_val* required;
        yyjson_arr_foreach(required_list, idx, max, required) {
            auto logical_name = yyjson_get_str(yyjson_obj_get(required, "logical-name"));
            std::println("  requires: {}", logical_name);
            task.depends_on.emplace_back(Dependency{.name = std::string(logical_name)});
            if (auto source_path = yyjson_obj_get(required, "source-path")) {
                auto path = fs::path(yyjson_get_str(source_path));
                std::println("    is header unit - {}", path.string());
                marked_header_units[path] = logical_name;
            }
        }
    }

    yyjson_doc_free(doc);
}
