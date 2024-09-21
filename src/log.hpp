#pragma once

#ifndef HARMONY_USE_IMPORT_STD
#include <format>
#include <syncstream>
#include <iostream>
#endif

constexpr std::string_view EndLogLine = "\u001B[0m\n";

enum class LogLevel : uint32_t
{
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

extern LogLevel log_level;

inline
bool IsLogLevel(LogLevel level)
{
    return uint32_t(level) >= uint32_t(log_level);
}

template<class... Args>
void Log(const std::format_string<Args...> fmt, Args&&... args)
{
    std::osyncstream os(std::cout);
    os << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogTrace(const std::format_string<Args...> fmt, Args&&... args)
{
    if (!IsLogLevel(LogLevel::Trace)) return;
    std::osyncstream os(std::cout);
    os << "[\u001B[90mTRACE\u001B[0m] \u001B[90m" << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogDebug(const std::format_string<Args...> fmt, Args&&... args)
{
    if (!IsLogLevel(LogLevel::Debug)) return;
    std::osyncstream os(std::cout);
    os << "[\u001B[96mDEBUG\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogInfo(const std::format_string<Args...> fmt, Args&&... args)
{
    if (!IsLogLevel(LogLevel::Info)) return;
    std::osyncstream os(std::cout);
    os << "[\u001B[94mINFO\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogWarn(const std::format_string<Args...> fmt, Args&&... args)
{
    if (!IsLogLevel(LogLevel::Warn)) return;
    std::osyncstream os(std::cout);
    os << "[\u001B[93mWARN\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogError(const std::format_string<Args...> fmt, Args&&... args)
{
    if (!IsLogLevel(LogLevel::Error)) return;
    std::osyncstream os(std::cout);
    os << "[\u001B[91mERROR\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

namespace harmony::formatting::detail {
    inline
    uint32_t DecimalsFor3SF(double value)
    {
        if (value < 10) return 2;
        if (value < 100) return 1;
        return 0;
    }
}

inline
std::string DurationToString(std::chrono::duration<double, std::nano> dur)
{
    using harmony::formatting::detail::DecimalsFor3SF;

    double nanos = dur.count();

    if (nanos >= 1e9) {
        double seconds = nanos / 1e9;
        return std::format("{:.{}f}s", seconds, DecimalsFor3SF(seconds));
    }

    if (nanos >= 1e6) {
        double millis = nanos / 1e6;
        return std::format("{:.{}f}ms", millis, DecimalsFor3SF(millis));
    }

    if (nanos >= 1e3) {
        double micros = nanos / 1e3;
        return std::format("{:.{}f}us", micros, DecimalsFor3SF(micros));
    }

    if (nanos >= 0) {
        return std::format("{:.{}f}ns", nanos, DecimalsFor3SF(nanos));
    }

    return "0";
}

inline
std::string ByteSizeToString(uint64_t bytes)
{
    using harmony::formatting::detail::DecimalsFor3SF;

    constexpr auto Exabyte   = 1ull << 60;
    if (bytes >= Exabyte) {
        double exabytes = bytes / double(Exabyte);
        return std::format("{:.{}f}EiB", exabytes, DecimalsFor3SF(exabytes));
    }

    constexpr auto Petabyte  = 1ull << 50;
    if (bytes >= Petabyte) {
        double petabytes = bytes / double(Petabyte);
        return std::format("{:.{}f}PiB", petabytes, DecimalsFor3SF(petabytes));
    }

    constexpr auto Terabyte  = 1ull << 40;
    if (bytes >= Terabyte) {
        double terabytes = bytes / double(Terabyte);
        return std::format("{:.{}f}TiB", terabytes, DecimalsFor3SF(terabytes));
    }

    constexpr auto Gigabyte = 1ull << 30;
    if (bytes >= Gigabyte) {
        double gigabytes = bytes / double(Gigabyte);
        return std::format("{:.{}f}GiB", gigabytes, DecimalsFor3SF(gigabytes));
    }

    constexpr auto Megabyte = 1ull << 20;
    if (bytes >= Megabyte) {
        double megabytes = bytes / double(Megabyte);
        return std::format("{:.{}f}MiB", megabytes, DecimalsFor3SF(megabytes));
    }

    constexpr auto Kilobyte = 1ull << 10;
    if (bytes >= Kilobyte) {
        double kilobytes = bytes / double(Kilobyte);
        return std::format("{:.{}f}KiB", kilobytes, DecimalsFor3SF(kilobytes));
    }

    if (bytes > 0) {
        return std::format("{} byte{}", double(bytes), bytes == 1 ? "" : "s");
    }

    return "0 bytes";
}
