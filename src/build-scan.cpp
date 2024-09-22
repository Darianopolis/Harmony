#include <xxhash.h>

#ifdef HARMONY_USE_IMPORT_STD
import std;
import std.compat;
#endif

#include <build.hpp>

#include "xxhash.h"

// :: // -> \n
// :: /* -> */
// :: # -> \n

static
bool ws(char c)
{
    return c == ' ' || c == '\t';
}

static
bool nl(char c)
{
    return c == '\n' || c == '\r';
}

static
bool wsnl(char c)
{
    return ws(c) || nl(c);
}

ScanResult ScanFile(const fs::path& path, std::string_view data, void(*callback)(void*, Component&), void* payload)
{
    // TODO: Handle comments and strings
    // TODO: Handle basic preprocessor evaluation
    //          #ifndef THING
    //          #if [!]THING
    //          #if [!]defined(THING)
    //          #else
    //          #endif
    //          #define THING
    //          #undef THING
    //          #define THING X

    HARMONY_IGNORE(path);

    auto start_time = chr::steady_clock::now();

    auto b = data.data();     // begin
    auto c = b;               // cur
    auto e = c + data.size(); // end

    auto escape_char = [](const char& c) -> std::string_view
    {
        if (c == '\n') return "\\n";
        if (c == '\t') return "\\t";
        if (c == '\r') return "\\r";
        return std::string_view(&c, 1);
    };

// #define log_trace(...) std::println(__VA_ARGS__)
#define log_trace(...)

    auto peek_false = [&](const char* c, const std::source_location& loc = std::source_location::current()) -> bool
    {
        log_trace("<{: 4}>[{: 4}] {}", loc.line(), c - b, escape_char(*c));
        HARMONY_IGNORE(c)
        return false;
    };

    std::string_view primary_module_name;

    while (c != e) {
        if (c > e)
            Error("Overrun buffer!");

        peek_false(c);

        if (*c == '#') {
            while (ws(*++c));
            log_trace("Checking for 'include' directive [{}]", std::string_view(c, c + 7));
            if (std::string_view(c, c + 7) != "include") {
                log_trace("  not found, skipping to next unescaped newline");
                // Skip any preprocessors other than include
                for (;;) {
                    while (!nl(*c)) peek_false(c++);
                    // if not escaped break immediately
                    if (*(c++ - 1) != '\\') break;
                    // check and fully escape crlf
                    if (*(c - 1) == '\r' && *c == '\n') c++;
                };
                log_trace("  reached end of preprocessor statement");
                continue;
            }
            log_trace("Found 'include' directive");
            c += 6;
            while (ws(*++c));
            peek_false(c);
            if (*c != '<' && *c != '"') continue;
            bool angled = *c == '<';
            auto start = c + 1;
            if (angled) {
                while (*++c != '>');
            } else {
                while (*++c != '"');
            }
            auto end = c++;
            LogTrace("#include {}{}{}", angled ? '<' : '"', std::string_view(start, end), angled ? '>' : '"');
        }

        if (*c == 'm') {
            log_trace("Found 'm' checking for module");
            bool is_exported = false;
            bool is_imported = false;
            bool is_header_unit = false;
            bool angled = false;
            const char* module_start = nullptr;
            const char *name_start{}, *name_end{};
            const char *part_start{}, *part_end{};

            if (c <= b || wsnl(*(c - 1))) {
                if (std::string_view(c + 1, c + 6) == "odule") {
                    log_trace("found 'module' keyword");
                    module_start = c;
                    c += 5;
                }
            } else if (*(c - 1) == 'i' && ((c - 1) <= b || wsnl(*(c - 2)))) {
                log_trace("preceeded by 'i', checking for 'import'");
                if (std::string_view(c - 1, c + 5) == "import") {
                    log_trace("found 'import' keyword");
                    is_imported = true;
                    module_start = c - 1;
                    c += 4;
                }
            }
            if (!module_start) {
                log_trace("not module or import, ignoring");
                ++c; continue;
            }
            peek_false(c);
            while (wsnl(*++c) || peek_false(c));
            peek_false(c);
            if (*c == ';') {
                // Global module fragment, ignore
                log_trace("global module fragment, ignore");
                ++c; continue;
            }
            if (*c == '"' || *c == '<') {
                is_header_unit = true;
                angled = *c == '<';
                name_start = c + 1;
                if (angled) {
                    while (*++c != '>');
                } else {
                    while (*++c != '"');
                }
                name_end = c;
                while (wsnl(*++c));
                if (*c != ';') {
                    log_trace("Expected semicolon to end import statement, found {}", *c);
                    continue;
                }
            } else if (!wsnl(*(c - 1))) {
                log_trace("expected '\"', '<' or whitespace after [module/import] keyword, found {}", *c);
                continue;
            } else {
                auto IsIdentifierChar = [](char ch) {
                    // return !(wsnl(ch) || ch == ';' || ch == ':');
                    return std::isalnum(ch) || ch == '.' || ch == '_';
                };
                log_trace("parsing name");
                name_start = c;
                while (peek_false(c) || IsIdentifierChar(*c)) c++;
                name_end = c;
                log_trace("end of name: [{}]", std::string_view(name_start, name_end));
                peek_false(c);
                while (wsnl(*c)) c++;
                peek_false(c);
                if (*c == ':') {
                    log_trace("parsing partition name");
                    while (wsnl(*++c));
                    part_start = c;
                    while (peek_false(c) || IsIdentifierChar(*c)) c++;
                    part_end = c;
                    log_trace("end of part name: [{}]", std::string_view(part_start, part_end));
                }
                if ((name_start == name_end) && std::string_view(part_start, part_end) == "private") {
                    // start of private module fragment, ignore
                    continue;
                }
                while (wsnl(*c)) c++;
                peek_false(c);
                if (*c != ';') {
                    log_trace("Expected semicolon, not valid module statement");
                    // not a semicolon terminated module statement, ignore
                    continue;
                }
            }
            auto p = module_start - 2;
            while (p >= b && !peek_false(p) &&wsnl(*p)) p--;
            if (p >= b) peek_false(c); else log_trace("no non-space characters before 'module' keyword");
            if (p - 5 >= b) {
                auto str = std::string_view(p - 5, p + 1);
                log_trace("checking for 'export' keyword in prefix: [{}]", str);
                if (str == "export") {
                    is_exported = true;
                }
            }
            auto name = std::string_view(name_start, name_end);
            auto part = std::string_view(part_start, part_end);
            if (is_imported) {
                if (!part.empty() && name.empty()) {
                    name = primary_module_name;
                }
            } else {
                primary_module_name = name;
            }
            if (is_header_unit) {
                LogTrace("{}import {}{}{};", is_exported ? "export " : "", angled ? '<' : '"', name, angled ? '>' : '"');
            } else {
                LogTrace("{}{} {}{}{};", is_exported ? "export " : "", is_imported ? "import" : "module", name, part.empty() ? "" : ":", part);
            }
            if (is_imported && !part.empty() && name != primary_module_name) {
                LogError("Module partition does not belong to primary module: [{}]", primary_module_name);
            }

            Component comp;
            comp.name = part.empty() ? std::string(name) : std::format("{}:{}", name, part);
            comp.type = is_header_unit ? Component::Type::HeaderUnit : Component::Type::Interface;
            comp.exported = is_exported;
            comp.imported = is_imported;
            comp.angled = angled;

            callback(payload, comp);
        }

        ++c;
    }

    auto end_time = chr::steady_clock::now();

    LogDebug("Parsed {} in {} ({}/s)", ByteSizeToString(data.size()), DurationToString(end_time - start_time),
        ByteSizeToString(uint64_t(double(data.size()) / chr::duration_cast<chr::duration<double>>(end_time - start_time).count())));

    auto hash = XXH64(data.data(), data.size(), 0);

    return ScanResult {
        .size =  data.size(),
        .hash = hash,
        .unique_name = std::format("{}.{:x}", path.filename().string(), hash),
    };
}
