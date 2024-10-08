#pragma once

#ifndef HARMONY_USE_IMPORT_STD
#include <filesystem>
#include <print>
#include <string_view>
#include <fstream>
#include <source_location>
#endif

#include <log.hpp>

namespace fs = std::filesystem;
namespace chr = std::chrono;

using namespace std::literals;

// -----------------------------------------------------------------------------

#define HARMONY_IGNORE(x) (void)x;

// -----------------------------------------------------------------------------

#define HARMONY_DEBUG_LINE LogDebug("@@@@ - {} @ {}", __LINE__, __FILE__);
#define HARMONY_DEBUG_EXPR(val) LogDebug(#val " = {}", (val));

// -----------------------------------------------------------------------------

#define HARMONY_CONCAT_INTERNAL(a, b) a##b
#define HARMONY_CONCAT(a, b) HARMONY_CONCAT_INTERNAL(a, b)
#define HARMONY_UNIQUE_VAR() HARMONY_CONCAT(HARMONY_var, __COUNTER__)

// -----------------------------------------------------------------------------

namespace harmony::detail
{
    template<typename Fn>
    struct DoOnceGuard
    {
        DoOnceGuard(Fn&& fn)
        {
            fn();
        }
    };

    template<typename Fn>
    struct OnExitGuard
    {
        Fn fn;

        OnExitGuard(Fn&& _fn)
            : fn(std::move(_fn))
        {}

        ~OnExitGuard()
        {
            fn();
        }
    };

    template<typename Fn>
    class DeferGuard
    {
        Fn fn;

    public:
        DeferGuard(Fn fn)
            : fn(std::move(fn))
        {}

        ~DeferGuard()
        {
            fn();
        }
    };
}

#define HARMONY_DO_ONCE(...) static ::harmony::detail::DoOnceGuard HARMONY_UNIQUE_VAR() = [__VA_ARGS__]
#define HARMONY_ON_EXIT(...) static ::harmony::detail::OnExitGuard HARMONY_UNIQUE_VAR() = [__VA_ARGS__]
#define HARMONY_DEFER(...)          ::harmony::detail::DeferGuard  HARMONY_UNIQUE_VAR() = [__VA_ARGS__]

// -----------------------------------------------------------------------------

#define HARMONY_DECORATE_FLAG_ENUM(EnumType)                                   \
    inline constexpr EnumType operator|(EnumType l, EnumType r) {              \
        return EnumType(std::to_underlying(l) | std::to_underlying(r));        \
    }                                                                          \
    inline constexpr EnumType operator|=(EnumType& l, EnumType r) {            \
        return l = l | r;                                                      \
    }                                                                          \
    inline constexpr bool operator>=(EnumType l, EnumType r) {                 \
        return std::to_underlying(r)                                           \
            == (std::to_underlying(l) & std::to_underlying(r));                \
    }                                                                          \
    inline constexpr bool operator&(EnumType l, EnumType r) {                  \
        return static_cast<std::underlying_type_t<EnumType>>(0)                \
            != (std::to_underlying(l) & std::to_underlying(r));                \
    }


// -----------------------------------------------------------------------------

// TODO: This should use Platform specific environment helper
inline const fs::path HarmonyDir = fs::path(std::getenv("USERPROFILE")) / ".harmony";
inline const fs::path HarmonyDataDir = HarmonyDir / "data";
inline const fs::path HarmonyTempDir = HarmonyDir / "tmp";
inline const fs::path HarmonyObjectDir = HarmonyDir / "out";

// -----------------------------------------------------------------------------

extern bool TraceCmds;

inline
void LogCmd(std::string_view cmd)
{
    if (TraceCmds) {
        LogTrace("<cmd> {}", cmd);
    }
}

struct HarmonySilentException {};

[[noreturn]] inline
void Error(std::string_view message)
{
    LogError("{}", message);
    throw HarmonySilentException{};
}

template<class... Args>
[[noreturn]] void Error(const std::format_string<Args...> fmt, Args&&... args)
{
    Error(std::vformat(fmt.get(), std::make_format_args(args...)));
}

// -----------------------------------------------------------------------------

enum class PathFormatOptions {
    Forward      =  1, // Use forward slashes
    Backward     =  2, // Use backward slashes
    QuoteSpaces  =  4, // Quote string if contains spaces
    Absolute     =  8, // Convert to absolute path
    Canonical    = 16, // Convert to canonical form
};

HARMONY_DECORATE_FLAG_ENUM(PathFormatOptions)

inline
std::string FormatPath(const fs::path& path, PathFormatOptions opts = PathFormatOptions::Forward | PathFormatOptions::QuoteSpaces)
{
    auto FormatPath_ = [](const fs::path& path, PathFormatOptions opts) {
        auto str = path.string();

        // File separators

        if (opts >= PathFormatOptions::Forward) {
            for (auto& c : str) if (c == '\\') c = '/';
        } else if (opts >= PathFormatOptions::Backward) {
            for (auto& c : str) if (c == '/') c = '\\';
        }

        // Quoting

        if (opts >= PathFormatOptions::QuoteSpaces) {
            bool quoted = false;
            for (auto c : str) {
                if (std::isspace(c)) quoted = true;
            }
            if (quoted) {
                return std::format("\"{}\"", str);
            }
        }

        return std::move(str);
    };

    // Absolute / canonical conversions

    if (opts >= PathFormatOptions::Canonical) {
        return FormatPath_(fs::canonical(path), opts);
    } else if (opts >= PathFormatOptions::Absolute) {
        return FormatPath_(fs::absolute(path), opts);
    } else {
        return FormatPath_(path, opts);
    }
}

inline
std::string ReadFileToString(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string str;
    str.resize(fs::file_size(path));
    in.read(str.data(), str.size());
    return str;
}

inline
void WriteStringToFile(const fs::path& path, std::string_view contents)
{
    std::ofstream out(path, std::ios::binary);
    out.write(contents.data(), contents.size());
}

// -----------------------------------------------------------------------------

template<typename Ret, typename... Types>
struct FuncBase {
    void* body;
    Ret(*fptr)(void*, Types...);

    template<typename ...Args>
    Ret operator()(Args&&... args) {
        return fptr(body, std::forward<Args>(args)...);
    }
};

template<typename Tx>
struct GetFunctionImpl {};

template<typename Ret, typename... Types>
struct GetFunctionImpl<Ret(Types...)> { using type = FuncBase<Ret, Types...>; };

template<typename Sig>
struct function_ref : GetFunctionImpl<Sig>::type {
    template<typename Fn>
    function_ref(Fn&& fn)
        : GetFunctionImpl<Sig>::type(&fn,
            []<typename... Args>(void*b, Args&&... args) -> auto {
                return std::forward<Fn>(*static_cast<Fn*>(b))(std::forward<Args>(args)...);
            })
    {};
};
