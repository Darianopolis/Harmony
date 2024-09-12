#pragma once

#include <backend/backend.hpp>

struct MsvcBackend : Backend
{
    ~MsvcBackend();

    virtual void FindDependencies(std::span<const Source> sources, std::vector<std::string>& dependency_info_p1689_json) const final override;
};
