#include "build.hpp"
#include "build-defs.hpp"

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

    std::vector<Task> tasks;

    for (auto& source : step.sources) {
        for (auto file : fs::recursive_directory_iterator(source.root,
                fs::directory_options::follow_directory_symlink |
                fs::directory_options::skip_permission_denied)) {

            auto ext = file.path().extension();
            auto type = SourceType::Unknown;

            if      (ext == ".cpp") type = SourceType::CppSource;
            else if (ext == ".hpp") type = SourceType::CppHeader;
            else if (ext == ".ixx") type = SourceType::CppInterface;

            if (type == SourceType::Unknown) continue;

            auto& task = tasks.emplace_back();
            task.source = { file, type };
            for (auto& include_dir : step.include_dirs) {
                task.include_dirs.emplace_back(include_dir);
            }
            for (auto& define : step.defines) {
                task.defines.emplace_back(define);
            }

            switch (task.source.type) {
                break;case SourceType::CppSource:    std::println("C++ Source    - {}", task.source.path.string());
                break;case SourceType::CppHeader:    std::println("C++ Header    - {}", task.source.path.string());
                break;case SourceType::CppInterface: std::println("C++ Interface - {}", task.source.path.string());
            }
        }
    }

    PrintStepHeader("Getting dependency info");

    std::vector<std::string> dependency_info;
    backend.FindDependencies(tasks, dependency_info);

    std::unordered_map<fs::path, std::string> marked_header_units;
    std::unordered_map<std::string, BuiltTask> built;

    PrintStepHeader("Parsing dependency P1689.json");

    for (int i = 0; i < tasks.size(); ++i) {
        auto& task = tasks[i];

        std::println("Dependency info for [{}]:", task.source.path.string());
        // std::println("{}", dependency_info[i]);

        ParseP1689(dependency_info[i], task, marked_header_units);
    }

    PrintStepHeader("Defining std modules");

    backend.GenerateStdModuleTask(tasks);

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

    PrintStepHeader("Trimming normal header tasks");

    std::erase_if(tasks, [](const auto& task) {
        return !task.is_header_unit && task.source.type == SourceType::CppHeader;
    });

    PrintStepHeader("Executing build steps");

    auto start = std::chrono::steady_clock::now();

    int passes = 0;
    int errors = 0;
    for (;;) {
        int remaining = 0;
        int executed = 0;

        if (passes > 0) {
            if (std::ranges::any_of(tasks, [](const auto& task) { return !task.completed; })) {
                std::println(" -- Pass [{:02}] --------------------------", passes + 1);
            }
        }

        std::vector<BuiltTask> completed;

        for (auto& task : tasks) {
            if (task.completed) continue;

            remaining++;

            if (!std::ranges::all_of(task.depends_on,
                    [&](auto& dependency) { return built.contains(dependency); })) {
                continue;
            }

            executed++;

            BuiltTask result{&task};
            if (backend.CompileTask(task, &result.obj, &result.bmi, built)) {
                completed.push_back(result);
            } else {
                errors++;
                task.completed = true;
            }
        }

        for (auto& build_task : completed) {
            for (auto& provided : build_task.task->produces) {
                built[provided] = build_task;
            }

            build_task.task->completed = true;
        }

        if (remaining && !executed) {
            if (errors) {
                std::println("Blocked after {} failed compilations", errors);
                return;
            }

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

        if (executed) {
            passes++;
        }

        if (remaining == 0) {
            break;
        }
    }

    auto end = std::chrono::steady_clock::now();

    PrintStepHeader("Reporting Build Stats");

    std::println("Passes   = {}", passes);
    std::println("Files    = {}", tasks.size());
    std::println("MT Score = {:.2f}", float(tasks.size()) / passes);
    std::println("Elapsed  = {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    if (step.output) {

        PrintStepHeader("Linking final object");

        backend.LinkStep(step.output.value(), built);

        PrintStepHeader("Running output");

        {
            auto cmd = (BuildDir / step.output->output).string();
            log_cmd(cmd);
            std::system(cmd.c_str());
        }
    }
}
