#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>
#include <configuration.hpp>

#include <backend/msvc/msvc-backend.hpp>
#include <backend/clang/clangcl-backend.hpp>

int main(int argc, char* argv[]) try
{
    auto start = chr::steady_clock::now();
    HARMONY_DEFER(&) {
        auto end = chr::steady_clock::now();
        LogInfo("--------------------------------------------------------------------------------");
        LogInfo("Elapsed: {}", DurationToString(end - start));
    };

    auto PrintUsage = []() {
        LogInfo("Usage: [build file] [-fetch] [-clean]");
        throw HarmonySilentException{};
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
            LogError("Unknown switch: {}", argv[i]);
            PrintUsage();
        }
    }

    auto config = ReadFileToString(argv[1]);

    if (fetch) {
        Fetch(config, clean);
    }

    std::unordered_map<std::string, Target> targets;
    std::vector<Task> tasks;
    ParseConfig(config, tasks, targets);

    MsvcBackend backend;
    Build(tasks, targets, backend);

    // HARMONY_IGNORE(argc, argv)

    // fs::path path = "D:/Dev/Cloned-Temp/Propolis/Source/Ranges/RegisterTraits.hpp";
    // std::string data;
    // {
    //     std::ifstream in(path, std::ios::binary);
    //     auto size = fs::file_size(path);
    //     data.resize(size + 16, '\0');
    //     in.read(data.data(), size);
    //     std::memset(data.data() + size, '\n', 16);
    // }

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
