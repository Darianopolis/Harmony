#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>
#include <generators/cmake-generator.hpp>

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

 -st                 :: Run build single threaded only for debugging

 -workspace <path>   :: Generate CMake workspace at given location

 -run <target>       :: Run the associated target after building
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
    bool multithreaded = true;
    std::optional<fs::path> workspace;
    std::optional<std::string> to_run;
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
        // Build single threaded
        // TODO: Allow user to select how many threads to use
        //       Should be set in profile?
        else if ("-st"sv == argv[i]) multithreaded = false;
        // Specify a workspace to create
        else if ("-workspace"sv == argv[i]) {
            if (++i >= argc) Error("Expected path after -worksapce");
            workspace = fs::path(argv[i]);
        }
        // Specify a target executable to run
        else if ("-run"sv == argv[i]) {
            if (++i >= argc) Error("Expected target name after -workspace");
            if (to_run) Error("Can only run one target");
            to_run = argv[i];
        }
        // Unknown switch
        else {
            LogError("Unknown switch: {}", argv[i]);
            PrintUsage();
        }
    }

    auto config = ReadFileToString(argv[1]);

    fs::create_directories(HarmonyDir);
    fs::create_directories(HarmonyDataDir);
    fs::create_directories(HarmonyTempDir);
    fs::create_directories(HarmonyObjectDir);
    BuildState state;
    std::unique_ptr<Backend> backend;
    if (use_clang) {
        backend = std::make_unique<ClangClBackend>();
    } else {
        backend = std::make_unique<MsvcBackend>();
    }
    state.backend = backend.get();

    // TODO: HUH
    state.backend->AddSystemIncludeDirs(state);

    // TODO: This is only required because targets require memory stability after ExpandTargets right now!
    //       store targets via pointers or use indices
    state.targets["std"].name = "std";

    ParseTargetsFile(state, config);
    FetchExternalData(state, clean_dependencies, fetch_dependencies);
    ExpandTargets(state);
    ScanDependencies(state, use_backend_dependency_scan);
    DetectAndInsertStdModules(state);
    SortDependencies(state);
    Flatten(state);
    if (workspace) {
        GenerateCMake(state, *workspace);
    }
    if (!Build(state, multithreaded)) {
        Error("Build failed, exiting");
    }
    LogInfo("Build success");
    if (to_run) {
        Run(state, *to_run);
    }
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
