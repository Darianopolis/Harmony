#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>
#include <configuration.hpp>

#include <backend/msvc/msvc-backend.hpp>

int main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char* argv[])
{
    if (argc < 2) {
        std::println("Expected root path");
        return 1;
    }

    Step step{
        .sources = {
            {.path = argv[1]}
        },
    };

    step.include_dirs.emplace_back(argv[1]);

    for (int i = 2; i < argc; ++i) {
        step.defines.emplace_back(argv[i]);
    }

    std::vector<Task> tasks;
    ExpandStep(step, tasks);

    MsvcBackend msvc;
    Build(tasks, step.output ? &step.output.value() : nullptr, msvc);
}
