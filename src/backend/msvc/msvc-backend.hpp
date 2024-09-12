#pragma once

#include "configuration.hpp"
#include <backend/backend.hpp>

struct MsvcBackend : Backend
{
    ~MsvcBackend();

    virtual void FindDependencies(std::span<const Source> sources, std::vector<std::string>& dependency_info_p1689_json) const final override;
    virtual void GenerateStdModuleTask(Task& task) const final override;
    virtual void CompileTask(const Task& task, fs::path* output_obj, fs::path* output_bmi, const std::unordered_map<std::string, BuiltTask>& built) const final override;
    virtual void LinkStep(const cfg::Artifact& artifact, const std::unordered_map<std::string, BuiltTask>& built) const final override;
};
