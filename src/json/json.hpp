#pragma once

#if 0

#include <core.hpp>

#include <vector>
#include <string_view>

namespace json {
    class pointer;

    class buffer {
        friend pointer;

    public:
        char *data;
        int32_t len;
        std::vector<uint32_t> tokens;

        buffer(std::string_view data);
        void tokenize();
        pointer begin();
        pointer end();

        void debug();
        std::string debugToken(uint32_t token) const noexcept;
    };

    class pointer {
        buffer *json;
        int32_t token;

        char tokenChar(int32_t token) const noexcept;
        int32_t nextExpectedBinder(int32_t token) const noexcept;

    public:
        pointer(buffer *json, int32_t token);

        pointer operator[](std::string_view key);
        pointer operator[](const char* key) { return this->operator[](std::string_view(key)); };

        std::string_view string() const;
        std::string_view key() const;
        double f64() const;
        double f64Fast() const;

        std::string debug() const noexcept;

        pointer next() const noexcept;
        pointer begin() const noexcept;
        pointer end() const noexcept;

        bool operator==(const pointer& other) const noexcept;
        pointer& operator++() noexcept;
        pointer operator*() const noexcept;

        operator bool() const noexcept;
    };
}

#endif
