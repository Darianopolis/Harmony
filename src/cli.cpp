#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>

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
 -fetch              :: Check for dependency updates
 -clean-deps         :: Clean fetch and build all dependencies

 -log-[level]        :: Set log level (trace, debug, info *default*, warn, error)
 -wait-on-close      :: Require enter press to close (for debugging purposes)

 -clang              :: Use the clang backend
 -msvc               :: Use the msvc backend

 -toolchain-dep-scan :: Use the toolchain (msvc, clang) provided dependency scan to verify dependencies
)");
        throw HarmonySilentException{};
    };

    if (argc < 2) {
        PrintUsage();
    }

    // TODO: This should be in profile configuration
    bool use_clang = false;
    bool use_backend_dependency_scan = false;
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
        // Use clang
        else if ("-clang"sv == argv[i]) use_clang = true;
        // Use msvc (default)
        else if ("-msvc"sv == argv[i]) use_clang = false;
        // Use vendor dependency scan
        else if ("-toolchain-dep-scan"sv == argv[i]) use_backend_dependency_scan = true;
        // Unknown switch
        else {
            LogError("Unknown switch: {}", argv[i]);
            PrintUsage();
        }
    }

    auto config = ReadFileToString(argv[1]);

    BuildState state;
    std::unique_ptr<Backend> backend;
    if (use_clang) {
        backend = std::make_unique<ClangClBackend>();
    } else {
        backend = std::make_unique<MsvcBackend>();
    }
    state.backend = backend.get();

    ParseConfig(config, state);
    Fetch(state, clean_dependencies, fetch_dependencies);
    ExpandTargets(state);
    Build(state, use_backend_dependency_scan);

    // HARMONY_IGNORE(argc, argv)
    //
    // log_level = LogLevel::Trace;
    // wait_on_close = true;
    //
    // fs::path path = "D:/Dev/Projects/harmony/src/generators/cmake-generator.cpp";
    // std::string data;
    // {
    //     std::ifstream in(path, std::ios::binary);
    //     if (!in.is_open()) {
    //         Error("Could not open file!");
    //     }
    //     auto size = fs::file_size(path);
    //     data.resize(size + 16, '\0');
    //     in.read(data.data(), size);
    //     std::memset(data.data() + size, '\n', 16);
    //     data[size] = '\n';
    //     data[size + 1] = '"';
    //     data[size + 2] = '>';
    //     data[size + 3] = '*';
    //     data[size + 4] = '/';
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
