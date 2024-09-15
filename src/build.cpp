#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include "build.hpp"
#include "build-defs.hpp"

#ifndef HARMONY_USE_IMPORT_STD
#include <print>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#endif

void PrintStepHeader(std::string_view name)
{
    std::println("==== {} ====", name);
}

void ExpandStep(const cfg::Step& step, std::vector<Task>& tasks)
{

    PrintStepHeader("Finding sources");

    uint32_t source_id = 0;

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

            task.unique_name = std::format("{}.{}", task.source.path.filename().replace_extension("").string(), source_id++);

            switch (task.source.type) {
                break;case SourceType::CppSource:    std::println("C++ Source    - {}", task.source.path.string());
                break;case SourceType::CppHeader:    std::println("C++ Header    - {}", task.source.path.string());
                break;case SourceType::CppInterface: std::println("C++ Interface - {}", task.source.path.string());
            }
        }
    }
}

void Build(std::vector<Task>& tasks, const cfg::Artifact* target, const Backend& backend)
{
    fs::create_directories(BuildDir);

    PrintStepHeader("Getting dependency info");

    std::vector<std::string> dependency_info;
    backend.FindDependencies(tasks, dependency_info);

    std::unordered_map<fs::path, std::string> marked_header_units;

    PrintStepHeader("Parsing dependency P1689.json");

    for (int i = 0; i < tasks.size(); ++i) {
        auto& task = tasks[i];

        std::println("Dependency info for [{}]:", task.source.path.string());
        // std::println("{}", dependency_info[i]);

        ParseP1689(dependency_info[i], task, marked_header_units);
    }

    PrintStepHeader("Defining std modules");

    {
        std::optional<Task> std_task, std_compat_task;
        for (auto& task : tasks) {
            for (auto& depends_on : task.depends_on) {
                if (!std_task && depends_on.name == "std") {
                    std::println("Detected usage of [std] module");
                    std_task = Task {
                        .source{.type = SourceType::CppInterface},
                        .unique_name = "std",
                        .produces = { "std" },
                        .external = true,
                    };
                }

                if (!std_compat_task && depends_on.name == "std.compat") {
                    std::println("Detected usage of [std.compat] module");
                    std_compat_task = Task {
                        .source{.type = SourceType::CppInterface},
                        .unique_name = "std.compat",
                        .produces = { "std.compat" },
                        .depends_on = { {"std"} },
                        .external = true,
                    };
                }

                if (std_task && std_compat_task) break;
            }
            if (std_task && std_compat_task) break;
        }

        backend.GenerateStdModuleTasks(
            std_task ? &*std_task : nullptr,
            std_compat_task ? &*std_compat_task : nullptr);
        if (std_task) tasks.emplace_back(std::move(*std_task));
        if (std_compat_task) tasks.emplace_back(std::move(*std_compat_task));
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
        task.external = true;
    }

    PrintStepHeader("Trimming normal header tasks");

    std::erase_if(tasks, [](const auto& task) {
        if (!task.is_header_unit && task.source.type == SourceType::CppHeader) {
            std::println("Header [{}] is not consumed as a header unit", task.unique_name);
            return true;
        }
        return false;
    });

    PrintStepHeader("Resolving dependencies");

    {
        auto FindTaskForProduced = [&](std::string_view name) -> Task* {
            for (auto& task : tasks) {
                for (auto& produced : task.produces) {
                    if (produced == name) return &task;
                }
            }
            return nullptr;
        };

        for (auto& task : tasks) {
            for (auto& depends_on : task.depends_on) {
                auto* depends_on_task = FindTaskForProduced(depends_on.name);
                if (!depends_on_task) {
                    error(std::format("No task provides [{}] required by [{}]", depends_on.name, task.unique_name));
                }
                depends_on.source = depends_on_task;
            }
        }
    }

    PrintStepHeader("Filling in backend task info");

    backend.AddTaskInfo(tasks);

    PrintStepHeader("Validating dependencies");

    // TODO: Check for illegal cycles (both in modules and includes)

    PrintStepHeader("Filtering up-to-date tasks");

    // Filter on immediate file changes

    for (auto& task : tasks) {
        // TODO: Filter for *all* tasks unless -clean specified
        if (!task.external) continue;

        if (task.is_header_unit) {
            if (!fs::exists(task.bmi) || fs::last_write_time(task.source.path) > fs::last_write_time(task.bmi)) {
                continue;
            }
        } else {
            if (!fs::exists(task.obj) || fs::last_write_time(task.source.path) > fs::last_write_time(task.obj)) {
                continue;
            }
        }

        task.state = TaskState::Complete;
    }

    // Filter on dependent module changes

    {
        auto CheckForUpdateRequired = [](this auto&& self, Task& task) {
            if (task.state != TaskState::Complete) return true;

            for (auto& dep : task.depends_on) {
                if (self(*dep.source)) {
                    task.state = TaskState::Waiting;
                    return true;
                }
            }

            return false;
        };

        for (auto& task : tasks) {
            CheckForUpdateRequired(task);
        }
    }

    // TODO: Filter on included header changes

    PrintStepHeader("Executing build steps");

    struct CompileStats {
        uint32_t to_compile = 0;
        uint32_t skipped = 0;
        uint32_t compiled = 0;
        uint32_t failed = 0;
    } stats;

    // Record num of skipped tasks for build stats
    for (auto& task : tasks) {
        if (task.state == TaskState::Complete) stats.skipped++;
        else if (task.state == TaskState::Waiting) stats.to_compile++;
    }

    auto start = std::chrono::steady_clock::now();

    bool mt_compile = true;
    {

        std::mutex m;
        std::atomic_uint32_t num_started = 0;
        std::atomic_uint32_t num_complete = 0;
        uint32_t last_num_complete = 0;

        for (;;) {
            uint32_t remaining = 0;
            uint32_t launched = 0;

            auto _num_started = num_started.load();
            auto _num_complete = num_complete.load();
            if (last_num_complete == _num_complete && _num_started > _num_complete) {
                // wait if no new complete and at least one task in-flight
                num_complete.wait(_num_complete);
            }

            for (auto& task : tasks) {
                auto cur_state = std::atomic_ref(task.state).load();
                if (cur_state == TaskState::Complete || cur_state == TaskState::Failed) continue;

                remaining++;

                if (cur_state != TaskState::Waiting) continue;

                if (!std::ranges::all_of(task.depends_on,
                        [&](auto& dependency) {
                            return std::atomic_ref(dependency.source->state) == TaskState::Complete;
                        })) {
                    continue;
                }

                num_started++;
                launched++;

                task.state = TaskState::Compiling;
                auto DoCompile = [&backend, &task, &num_complete] {
                    auto success = backend.CompileTask(task);

                    std::atomic_ref(task.state) = success ? TaskState::Complete : TaskState::Failed;

                    num_complete++;
                    num_complete.notify_all();
                };

                if (mt_compile) {
                    std::thread t{DoCompile};
                    t.detach();
                } else {
                    DoCompile();
                }
            }

            if (remaining
                    && _num_started == _num_complete
                    && launched == 0) {
                uint32_t num_errors = 0;
                for (auto& task : tasks) {
                    if (task.state == TaskState::Failed) num_errors++;
                }
                if (num_errors) {
                    std::println("Blocked after {} failed compilations", num_errors);
                    break;
                }

                std::println("Unable to start any additional tasks");
                for (auto& task : tasks) {
                    if (task.state == TaskState::Complete) continue;
                    std::println("task[{}] blocked", task.source.path.string());
                    for (auto& dep : task.depends_on) {
                        if (dep.source->state == TaskState::Complete) continue;
                        std::println(" - {}{}", dep.name, dep.source->state == TaskState::Failed ? " (failed)" : "");
                    }
                }
                break;
            }

            if (remaining == 0) {
                break;
            }
        }
    }

    auto end = std::chrono::steady_clock::now();

    PrintStepHeader("Reporting Build Stats");

    {
        uint32_t num_complete = 0;
        for (auto& task : tasks) {
            if (task.state == TaskState::Complete) num_complete++;
            else if (task.state == TaskState::Failed) stats.failed++;
        }
        stats.compiled = num_complete - stats.skipped;

        std::println("Compiled = {} / {}", stats.compiled, stats.to_compile);
        if (stats.skipped) std::println("  Skipped = {}", stats.skipped);
        if (stats.failed)  std::println("  Failed  = {}", stats.failed);
        if (stats.compiled < stats.to_compile) std::println("  Blocked = {}", stats.to_compile - (stats.compiled + stats.failed));
        std::println("Elapsed  = {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    }

    if (target && stats.compiled == stats.to_compile) {

        PrintStepHeader("Linking final object");

        backend.LinkStep(*target, tasks);

        PrintStepHeader("Running output");

        {
            auto cmd = (BuildDir / target->output).string();
            log_cmd(cmd);
            std::system(cmd.c_str());
        }
    }
}
