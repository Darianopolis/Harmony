#pragma once

#include <core.hpp>
#include <build-defs.hpp>
#include <configuration.hpp>

#include <unordered_map>
#include <span>

struct Backend {
    virtual ~Backend() = 0;

    virtual void FindDependencies(std::span<const Source> sources, std::vector<std::string>& dependency_info_p1689_json) const = 0;
    virtual void GenerateStdModuleTask(Task& task) const = 0;
    virtual void CompileTask(const Task& task, fs::path* output_obj, fs::path* output_bmi, const std::unordered_map<std::string, BuiltTask>& built) const = 0;
    virtual void LinkStep(const cfg::Artifact& artifact, const std::unordered_map<std::string, BuiltTask>& built) const = 0;
};

inline
Backend::~Backend() = default;
