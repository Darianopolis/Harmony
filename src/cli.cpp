#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>
#include <configuration.hpp>

#include <backend/msvc-backend.hpp>
#include <backend/clangcl-backend.hpp>

int main(int argc, char* argv[]) try
{
     bool wait_on_close = false;

     auto start = chr::steady_clock::now();
     HARMONY_DEFER(&) {
         auto end = chr::steady_clock::now();
         LogInfo("--------------------------------------------------------------------------------");
         LogInfo("Elapsed: {}", DurationToString(end - start));
         if (wait_on_close) {
             log_level = LogLevel::Info;
             LogInfo("Press enter to close");
             std::cin.get();
         }
     };

    auto PrintUsage = [] {
        LogInfo(R"(Usage: [build file] <flags...>
 -fetch         :: Check for dependency updates
 -clean-deps    :: Clean fetch and build all dependencies
 -log-[level]   :: Set log level (trace, debug, info *default*, warn, error)
 -wait-on-close :: Require enter press to close (for debugging purposes)
)");
        throw HarmonySilentException{};
    };

    if (argc < 2) {
        PrintUsage();
    }

    bool fetch_dependencies = false;
    bool clean_dependencies = false;
    for (int i = 2; i < argc; ++i) {
        // Check for updates
        if ("-fetch"sv == argv[i]) fetch_dependencies = true;
        // Clean rebuild dependencies
        else if ("-clean-deps"sv == argv[i]) clean_dependencies = true;
        // Logging flags
        else if ("-log-trace"sv == argv[i]) log_level = LogLevel::Trace;
        else if ("-log-debug"sv == argv[i]) log_level = LogLevel::Debug;
        else if ("-log-info"sv == argv[i]) log_level = LogLevel::Info;
        else if ("-log-warn"sv == argv[i]) log_level = LogLevel::Warn;
        else if ("-log-error"sv == argv[i]) log_level = LogLevel::Error;
        // CLion external terminal utilities
        else if ("-wait-on-close"sv == argv[i]) wait_on_close = true;
        // Unknown switch
        else {
            LogError("Unknown switch: {}", argv[i]);
            PrintUsage();
        }
    }

    auto config = ReadFileToString(argv[1]);

    Fetch(config, clean_dependencies, fetch_dependencies);

    std::unordered_map<std::string, Target> targets;
    std::vector<Task> tasks;
    ParseConfig(config, tasks, targets);

    MsvcBackend backend;
    // ClangClBackend backend;
    Build(tasks, targets, backend);

    // HARMONY_IGNORE(argc, argv)
    //
    // log_level = LogLevel::Trace;
    // wait_on_close = true;
    //
    // fs::path path = "D:/Dev/Cloned-Temp/Propolis/Source/Casts/Casts.ixx";
    // std::string data;
    // {
    //     std::ifstream in(path, std::ios::binary);
    //     auto size = fs::file_size(path);
    //     data.resize(size + 16, '\0');
    //     in.read(data.data(), size);
    //     std::memset(data.data() + size, '\n', 16);
    //     data[size] = '\n';
    //     data[size + 1] = '"';
    //     data[size + 2] = '>';
    // }
    //
    // LogDebug("Scanning file: [{}]", path.string());
    // ScanFile(path, data, [](Component&){});
}
catch (const std::exception& e)
{
    LogError("{}", e.what());
}
catch (std::error_code code)
{
    LogError("({}) {}", code.value(), code.message());
}
catch (HarmonySilentException)
{
    // do nothing
}
catch (...)
{
    LogError("Unknown Error");
}
