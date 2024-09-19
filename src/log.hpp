#pragma once

#include <format>
#include <syncstream>
#include <iostream>

constexpr std::string_view EndLogLine = "\u001B[0m\n";

template<class... Args>
void Log(const std::format_string<Args...> fmt, Args&&... args)
{
    std::osyncstream os(std::cout);
    os << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogInfo(const std::format_string<Args...> fmt, Args&&... args)
{
    std::osyncstream os(std::cout);
    os << "[\u001B[94mINFO\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogDebug(const std::format_string<Args...> fmt, Args&&... args)
{
    std::osyncstream os(std::cout);
    os << "[\u001B[96mDEBUG\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogTrace(const std::format_string<Args...> fmt, Args&&... args)
{
    std::osyncstream os(std::cout);
    os << "[\u001B[90mTRACE\u001B[0m] \u001B[90m" << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogError(const std::format_string<Args...> fmt, Args&&... args)
{
    std::osyncstream os(std::cout);
    os << "[\u001B[91mERROR\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

template<class... Args>
void LogWarn(const std::format_string<Args...> fmt, Args&&... args)
{
    std::osyncstream os(std::cout);
    os << "[\u001B[93mWARN\u001B[0m] " << std::vformat(fmt.get(), std::make_format_args(args...)) << EndLogLine;
}

inline
std::string DurationToString(std::chrono::duration<double, std::nano> dur)
{
    double nanos = dur.count();

    if (nanos > 1e9) {
        double seconds = nanos / 1e9;
        uint32_t decimals = 2 - uint32_t(std::log10(seconds));
        return std::format("{:.{}f}s",seconds, decimals);
    }

    if (nanos > 1e6) {
        double millis = nanos / 1e6;
        uint32_t decimals = 2 - uint32_t(std::log10(millis));
        return std::format("{:.{}f}ms", millis, decimals);
    }

    if (nanos > 1e3) {
        double micros = nanos / 1e3;
        uint32_t decimals = 2 - uint32_t(std::log10(micros));
        return std::format("{:.{}f}us", micros, decimals);
    }

    if (nanos > 0) {
        uint32_t decimals = 2 - uint32_t(std::log10(nanos));
        return std::format("{:.{}f}ns", nanos, decimals);
    }

    return "0";
}
