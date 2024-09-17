#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>
#include <configuration.hpp>

#include <backend/msvc/msvc-backend.hpp>

int main(int argc, char* argv[])
{
    auto PrintUsage = [] {
        error("Usage: [build file] [-fetch] [-clean]");
    };

    if (argc < 2) {
        PrintUsage();
    }

    bool fetch = false;
    bool clean = false;
    for (int i = 2; i < argc; ++i) {
             if ("-fetch"sv == argv[i]) fetch = true;
        else if ("-clean"sv == argv[i]) clean = true;
        else {
            std::println("Unknown switch: {}", argv[i]);
            PrintUsage();
        }
    }

    auto config = read_file(argv[1]);

    if (fetch) {
        Fetch(config, clean);
    }

    std::vector<Task> tasks;
    ParseConfig(config, tasks);

    MsvcBackend msvc;
    Build(tasks, nullptr, msvc);
}
