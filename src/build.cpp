#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include "build.hpp"

#include <generators/cmake-generator.hpp>

#ifndef HARMONY_USE_IMPORT_STD
#include <print>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <fstream>
#endif

#include <json.hpp>
#include <backend//backend.hpp>

void Build(BuildState& state, bool use_backend_dependency_scan)
{
    fs::create_directories(BuildDir);

    LogDebug("Getting dependency info");

    std::unordered_map<fs::path, std::string> marked_header_units;

    auto backend_scan_differences = 0;

    std::vector<std::string> dependency_info;
    if (use_backend_dependency_scan) {
        dependency_info.resize(state.tasks.size());
#pragma omp parallel for
        for (uint32_t i = 0; i < state.tasks.size(); ++i) {
            state.backend->FindDependencies(state.tasks[i], dependency_info[i]);
        }
    }

    {
        std::string data;
        std::unordered_map<std::string, int> produced_set;
        std::unordered_map<std::string, int> required_set;
        for (auto i = 0; i < state.tasks.size(); ++i) {
            auto& task = state.tasks[i];
            LogDebug("Scanning file: [{}]", task.source.path.string());

            if (use_backend_dependency_scan) {
                produced_set.clear();
                required_set.clear();

                LogDebug("  Backend results:");

                JsonDocument doc(dependency_info[i]);
                for (auto rule : doc.root()["rules"]) {
                    for (auto provided : rule["provides"]) {
                        auto logical_name = provided["logical-name"].string();
                        produced_set[logical_name]++;
                        LogTrace("produces module [{}]", logical_name);
                        task.produces.emplace_back(logical_name);
                    }

                    for (auto required : rule["requires"]) {
                        auto logical_name = required["logical-name"].string();
                        required_set[logical_name]++;
                        // LogTrace("  requires: {}", logical_name);
                        task.depends_on.emplace_back(Dependency{.name = std::string(logical_name)});
                        if (auto source_path = required["source-path"]) {
                            auto path = fs::path(source_path.string());
                            // LogTrace("    is header unit - {}", path.string());
                            marked_header_units[path] = logical_name;
                            LogTrace("requires header [{}]", path.string());
                        } else {
                            LogTrace("requires module [{}]", logical_name);
                        }
                    }
                }

                LogDebug("  build-deps results:");
            }

            {
                std::ifstream in(task.source.path, std::ios::binary | std::ios::ate);
                auto size = fs::file_size(task.source.path);
                in.seekg(0);
                data.resize(size + 16, '\0');
                in.read(data.data(), size);
                std::memset(data.data() + size, '\n', 16);
                // TODO: This should be handled in ScanFile
                data[size] = '\n';
                data[size + 1] = '"';
                data[size + 2] = '>';
                data[size + 3] = '*';
                data[size + 4] = '/';
            }

            auto scan_result = ScanFile(task.source.path, data, [&](Component& comp) {
                if (!comp.imported && comp.exported) {
                    if (use_backend_dependency_scan) {
                        produced_set[comp.name]--;
                    } else {
                        task.produces.emplace_back(std::move(comp.name));
                    }
                } else {
                    if (use_backend_dependency_scan) {
                        required_set[comp.name]--;
                    } else {
                        task.depends_on.emplace_back(Dependency{.name = std::move(comp.name)});
                    }
                }
            });

            task.unique_name = scan_result.unique_name;

            if (use_backend_dependency_scan) {
                for (auto&[r, s] : produced_set) {
                    if (s > 0) {
                        backend_scan_differences++;
                        LogError("MSVC produces [{}] not found by build-scan", r);
                    } else if (s < 0) {
                        backend_scan_differences++;
                        LogError("Found produces [{}] not present in MSVC output", r);
                    }
                }

                for (auto&[p, s] : required_set) {
                    if (s > 0) {
                        backend_scan_differences++;
                        LogError("MSVC requires [{}] not found by build-scan", p);
                    } else if (s < 0) {
                        backend_scan_differences++;
                        LogError("build-scan requires [{}] not present in MSVC output", p);
                    }
                }
            }
        }

        if (backend_scan_differences) {
            Error("Discrepancy between backend and build-scan outputs, aborting compilation");
        }
    }

    LogDebug("Defining std modules");

    Target std_target;
    std_target.name = "std";
    {
        std::optional<Task> std_task, std_compat_task;
        for (auto& task : state.tasks) {
            for (auto& depends_on : task.depends_on) {
                if (depends_on.name == "std") {
                    if (!std_task) {
                        LogInfo("Detected usage of [std] module");
                        std_task = Task {
                            .target = &std_target,
                            .source{.type = SourceType::CppInterface},
                            .unique_name = "std",
                            .produces = { "std" },
                            .external = true,
                        };
                    }

                    task.target->flattened_imports.emplace(&std_target);
                }

                if (depends_on.name == "std.compat") {
                    if (!std_compat_task) {
                        LogInfo("Detected usage of [std.compat] module");
                        std_compat_task = Task {
                            .target = &std_target,
                            .source{.type = SourceType::CppInterface},
                            .unique_name = "std.compat",
                            .produces = { "std.compat" },
                            .depends_on = { {"std"} },
                            .external = true,
                        };
                    }

                    task.target->flattened_imports.emplace(&std_target);
                }
            }
        }

        state.backend->GenerateStdModuleTasks(
            std_task ? &*std_task : nullptr,
            std_compat_task ? &*std_compat_task : nullptr);
        if (std_task) state.tasks.emplace_back(std::move(*std_task));
        if (std_compat_task) state.tasks.emplace_back(std::move(*std_compat_task));
    }

    LogDebug("Marking header units");

    for (auto& task : state.tasks) {
        auto path = fs::absolute(task.source.path);
        auto iter = marked_header_units.find(path);
        if (iter == marked_header_units.end()) continue;
        task.is_header_unit = true;
        task.produces.emplace_back(iter->second);

        LogTrace("task[{}].is_header_unit = {}", task.source.path.string(), task.is_header_unit);

        marked_header_units.erase(path);
    }

    // TODO: This should be a separate stage launched by cli.cpp
    {
        GenerateCMake(".", state.tasks, state.targets);
    }

    LogDebug("Generating external header unit tasks");

    {
        uint32_t ext_header_uid = 0;
        for (auto[path, logical_name] : marked_header_units) {
            LogTrace("external header unit[{}] -> {}", path.string(), logical_name);

            auto& task = state.tasks.emplace_back();
            task.source.path = path;
            task.source.type = SourceType::CppHeader;
            task.is_header_unit = true;
            task.produces.emplace_back(logical_name);
            task.external = true;

            task.unique_name = std::format("{}.{}E", path.filename().string(), ext_header_uid++);
        }
    }

    LogDebug("Trimming normal header tasks");

    std::erase_if(state.tasks, [](const auto& task) {
        if (!task.is_header_unit && task.source.type == SourceType::CppHeader) {
            LogTrace("Header [{}] is not consumed as a header unit", task.unique_name);
            return true;
        }
        return false;
    });

    LogDebug("Resolving dependencies");

    {
        auto FindTaskForProduced = [&](std::string_view name) -> Task* {
            for (auto& task : state.tasks) {
                for (auto& produced : task.produces) {
                    if (produced == name) return &task;
                }
            }
            return nullptr;
        };

        for (auto& task : state.tasks) {
            for (auto& depends_on : task.depends_on) {
                auto* depends_on_task = FindTaskForProduced(depends_on.name);
                if (!depends_on_task) {
                    Error(std::format("No task provides [{}] required by [{}]", depends_on.name, task.unique_name));
                }
                depends_on.source = depends_on_task;
            }
        }
    }

    bool compute_dependency_stats = true;
    if (compute_dependency_stats) {
        LogDebug("Calculating dependency stats");

        std::unordered_map<const void*, uint32_t> cache;
        auto FindMaxDepth = [&](this auto&& self, const Task& task) {
            if (cache.contains(&task)) {
                return cache.at(&task);
            }

            uint32_t max_depth = 0;

            for (auto& dep : task.depends_on) {
                max_depth = std::max(max_depth, self(*dep.source));
            }

            cache[&task] = max_depth + 1;
            return max_depth + 1;
        };

        {
            uint32_t max_depth  = 0;
            for (auto& task : state.tasks) {
                max_depth = std::max(max_depth, FindMaxDepth(task));
            }
        }

        uint32_t i = 0;
        auto ReadMaxDepthChain = [&](this auto&& self, const Task& task) -> void {
            LogTrace("[{}] = {}", i++, task.source.path.string());

            uint32_t max_depth = 0;
            const Task* max_task = nullptr;

            for (auto& dep : task.depends_on) {
                if (cache.at(dep.source) >= max_depth) {
                    max_depth = cache.at(dep.source);
                    max_task = dep.source;
                }
            }

            if (max_depth > 0) {
                self(*max_task);
            }
        };

        {
            uint32_t max_depth = 0;
            const Task* max_task = nullptr;

            for (auto& task : state.tasks) {
                if (cache.at(&task) >= max_depth) {
                    max_depth = cache.at(&task);
                    max_task = &task;
                }
            }

            if (max_task) {
                LogDebug("Maximum task depth = {}", max_depth);
                ReadMaxDepthChain(*max_task);
            }
        }
    }

    LogDebug("Filling in backend task info");

    state.backend->AddTaskInfo(state.tasks);

    LogDebug("Validating dependencies");

    // TODO: Check for illegal cycles (both in modules and includes)

    LogDebug("Filtering up-to-date tasks");

    // Filter on immediate file changes

    for (auto& task : state.tasks) {
        // TODO: Filter for *all* tasks unless -clean specified
        // if (!task.external) continue;
        // if (task.target->name == "panta-rhei" || task.target->name == "propolis") continue;

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
        std::unordered_map<void*, bool> cache;
        auto CheckForUpdateRequired = [&](this auto&& self, Task& task) {
            if (cache.contains(&task)) {
                return cache.at(&task);
            }
            if (task.state != TaskState::Complete) {
                cache[&task] = true;
                return true;
            }

            for (auto& dep : task.depends_on) {
                if (self(*dep.source)) {
                    task.state = TaskState::Waiting;
                    cache[&task] = true;
                    return true;
                }
            }

            cache[&task] = false;
            return false;
        };

        for (auto& task : state.tasks) {
            CheckForUpdateRequired(task);
        }
    }

    // TODO: Filter on included header changes

    LogDebug("Executing build steps");

    struct CompileStats {
        uint32_t to_compile = 0;
        uint32_t skipped = 0;
        uint32_t compiled = 0;
        uint32_t failed = 0;
    } stats;

    // Record num of skipped tasks for build stats
    for (auto& task : state.tasks) {
        if (task.state == TaskState::Complete) stats.skipped++;
        else if (task.state == TaskState::Waiting) stats.to_compile++;
    }

    // if (true) {
    //     backend.GenerateCompileCommands(tasks);
    //     return;
    // }

    LogInfo("Compiling {} files ({} up to date)", stats.to_compile, stats.skipped);

    auto start = chr::steady_clock::now();

    bool mt_compile = true;
    {
        std::mutex m;
        std::atomic_uint32_t num_started = 0;
        std::atomic_uint32_t num_complete = 0;
        uint32_t last_num_complete = 0;

        // bool abort = false;

        for (;;) {
            uint32_t remaining = 0;
            uint32_t launched = 0;

            auto _num_started = num_started.load();
            auto _num_complete = num_complete.load();
            if (last_num_complete == _num_complete && _num_started > _num_complete) {
                // wait if no new complete and at least one task in-flight
                num_complete.wait(_num_complete);
            }
            last_num_complete = _num_complete;

            for (auto& task : state.tasks) {
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
                auto DoCompile = [&state, &task, &num_complete] {
                    auto success = state.backend->CompileTask(task);

                    std::atomic_ref(task.state) = success ? TaskState::Complete : TaskState::Failed;

                    num_complete++;
                    num_complete.notify_all();

                    return success;
                };

                if (mt_compile) {
                    std::thread t{DoCompile};
                    t.detach();
                } else {
                    [[maybe_unused]] auto success = DoCompile();
                    // if (success) break;
                    // if (!success) {
                    //     log_error("Ending compilation early after error");
                    //     abort = true;
                    //     break;
                    // }
                }
            }

            // if (abort) {
            //     break;
            // }

            if (remaining
                    && _num_started == _num_complete
                    && launched == 0) {
                uint32_t num_errors = 0;
                for (auto& task : state.tasks) {
                    if (task.state == TaskState::Failed) num_errors++;
                }
                if (num_errors) {
                    LogError("Blocked after {} failed compilations", num_errors);
                    break;
                }

                LogError("Unable to start any additional tasks");
                for (auto& task : state.tasks) {
                    if (task.state == TaskState::Complete) continue;
                    LogError("task[{}] blocked", task.source.path.string());
                    for (auto& dep : task.depends_on) {
                        if (dep.source->state == TaskState::Complete) continue;
                        LogError(" - {}{}", dep.name, dep.source->state == TaskState::Failed ? " (failed)" : "");
                    }
                }
                break;
            }

            if (remaining == 0) {
                break;
            }
        }
    }

    auto end = chr::steady_clock::now();

    LogInfo("Reporting Build Stats");

    {
        uint32_t num_complete = 0;
        for (auto& task : state.tasks) {
            if (task.state == TaskState::Complete) num_complete++;
            else if (task.state == TaskState::Failed) stats.failed++;
        }
        stats.compiled = num_complete - stats.skipped;

        if (stats.skipped) {
            LogInfo("Compiled = {} / {} ({} skipped)", stats.compiled, stats.to_compile, stats.skipped);
        } else {
            LogInfo("Compiled = {} / {}", stats.compiled, stats.to_compile);
        }
        if (stats.failed)  LogWarn("  Failed  = {}", stats.failed);
        if (stats.compiled < stats.to_compile) LogWarn("  Blocked = {}", stats.to_compile - (stats.compiled + stats.failed));
        LogInfo("Elapsed  = {}", DurationToString(end - start));

    }

    if (stats.compiled == stats.to_compile) {

        LogInfo("Linking target executables");

        for (auto&[_, target] : state.targets) {
            if (!target.executable) continue;

            LogInfo("Linking [{}] from [{}]", target.executable->path.string(), target.name);
            auto res = state.backend->LinkStep(target, state.tasks);
            if (!res) {
                LogError("Error linking [{}] from [{}]", target.executable->path.string(), target.name);
            }
        }
    }
}
