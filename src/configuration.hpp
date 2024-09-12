#pragma once

#include <core.hpp>

namespace cfg {
    struct Sources {
        fs::path root;
    };

    struct Artifact {
        fs::path output;
    };

    struct Step {
        std::vector<Sources> sources;
        std::optional<Artifact> output;
    };
}
