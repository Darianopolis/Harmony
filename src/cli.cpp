#include <build.hpp>
#include <configuration.hpp>

#include <backend/msvc/msvc-backend.hpp>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::println("Expected root path");
        return EXIT_FAILURE;
    }

    cfg::Step step{
        .sources = {{
            .root = argv[1],
        }},
    };

    step.include_dirs.emplace_back(argv[1]);

    for (int i = 2; i < argc; ++i) {
        step.defines.emplace_back(argv[i]);
    }

    MsvcBackend msvc;
    Build(step, msvc);
}
