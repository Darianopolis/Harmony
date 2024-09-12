#include "build.hpp"
#include "build-defs.hpp"

#include <json/json.hpp>

#include <print>
#include <string_view>
#include <unordered_map>

void PrintStepHeader(std::string_view name)
{
    std::println("==== {} ====", name);
}

void Build(const cfg::Step& step, const Backend& backend)
{
    fs::create_directories(BuildDir);

    // 1. Find all sources from roots
    // 2. Compute dependencies
    // 3. Generate compile commands

    PrintStepHeader("Finding sources");

    std::vector<Source> sources;

    for (auto& source : step.sources) {
        for (auto file : fs::recursive_directory_iterator(source.root,
                fs::directory_options::follow_directory_symlink |
                fs::directory_options::skip_permission_denied)) {

            auto ext = file.path().extension();
            if      (ext == ".cpp") sources.emplace_back(file, SourceType::CppSource);
            else if (ext == ".hpp") sources.emplace_back(file, SourceType::CppHeader);
            else if (ext == ".ixx") sources.emplace_back(file, SourceType::CppInterface);
        }
    }

    for (auto& source : sources) {
        switch (source.type) {
            break;case SourceType::CppSource:    std::println("C++ Source    - {}", source.path.string());
            break;case SourceType::CppHeader:    std::println("C++ Header    - {}", source.path.string());
            break;case SourceType::CppInterface: std::println("C++ Interface - {}", source.path.string());
        }
    }

    PrintStepHeader("Getting dependency info");

    std::vector<std::string> dependency_info;
    backend.FindDependencies(sources, dependency_info);

    std::unordered_map<fs::path, std::string> marked_header_units;
    std::unordered_map<std::string, BuiltTask> built;
    std::vector<Task> tasks;

    PrintStepHeader("Defining std modules");

    {
        auto& task = tasks.emplace_back();
        backend.GenerateStdModuleTask(task);
    }

    PrintStepHeader("Parsing dependency P1689.json");

    for (int i = 0; i < sources.size(); ++i) {
        std::println("Dependency info for [{}]:", sources[i].path.string());

        auto& task = tasks.emplace_back();
        task.source = sources[i];

        json::buffer json(dependency_info[i]);
        json.tokenize();
        auto rule = json.begin()["rules"].begin();

        for (auto provided : rule["provides"]) {
                auto logical_name = provided["logical-name"].string();
                std::println("  provides: {}", logical_name);
                task.produces.emplace_back(logical_name);
        }

        for (auto required : rule["requires"]) {
            auto logical_name = required["logical-name"].string();
            std::println("  requires: {}", logical_name);
            task.depends_on.emplace_back(logical_name);
            if (required["source-path"]) {
                auto path = fs::path(required["source-path"].string());
                std::println("    is header unit - {}", path.string());
                marked_header_units[path] = logical_name;
            }
        }
    }

    PrintStepHeader("Marking header units");

    for (auto& task : tasks) {
        auto path = fs::absolute(task.source.path);
        auto iter = marked_header_units.find(path);
        if (iter == marked_header_units.end()) continue;
        task.is_header_unit = true;
        task.produces.emplace_back(iter->second);

        std::println("task[{}].is_header_unit = {}", task.source.path.string(), task.is_header_unit);

        marked_header_units.erase(path);
    }

    PrintStepHeader("Generating external header unit tasks");

    for (auto[path, logical_name] : marked_header_units) {
        std::println("external header unit[{}] -> {}", path.string(), logical_name);

        auto& task = tasks.emplace_back();
        task.source.path = path;
        task.source.type = SourceType::CppHeader;
        task.is_header_unit = true;
        task.produces.emplace_back(logical_name);
    }

    PrintStepHeader("Executing build steps");

    for (;;) {
        int remaining = 0;
        int executed = 0;

        for (auto& task : tasks) {
            if (task.completed) continue;

            remaining++;

            if (!std::ranges::all_of(task.depends_on,
                    [&](auto& dependency) { return built.contains(dependency); })) {
                continue;
            }

            executed++;

            BuiltTask result{&task};
            backend.CompileTask(task, &result.obj, &result.bmi, built);

            for (auto& provided : task.produces) {
                built[provided] = result;
            }

            task.completed = true;
        }

        if (remaining && !executed) {
            std::println("Error: Illegal dependency chain detected");
            for (auto& task : tasks) {
                if (task.completed) continue;
                std::println("task[{}] blocked", task.source.path.string());
                for (auto& dep : task.depends_on) {
                    if (built.contains(dep)) continue;
                    std::println(" - {}", dep);
                }
            }
            return;
        }

        if (remaining == 0) {
            break;
        }
    }

    if (step.output) {

        PrintStepHeader("Linking final object");

        backend.LinkStep(step.output.value(), built);

        PrintStepHeader("Running output");

        {
            auto cmd = (BuildDir / step.output->output).string();
            std::println("[cmd] {}", cmd);
            std::system(cmd.c_str());
        }
    }
}
