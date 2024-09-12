#include "json.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <array>

constexpr auto WHITESPACE{[]() {
        constexpr size_t size = 256;
        std::array<bool, size> result{};
        result[' '] = true;
        result['\r'] = true;
        result['\t'] = true;
        result['\n'] = true;
        result[','] = true;
        return result;
}()};

constexpr auto RAW_VALUE_END{[]() {
        constexpr size_t size = 256;
        std::array<bool, size> result{};
        result[' '] = true;
        result['\r'] = true;
        result['\t'] = true;
        result['\n'] = true;
        result['/'] = true;
        result[','] = true;
        result['}'] = true;
        result[']'] = true;
        return result;
}()};

namespace json {
    buffer::buffer(std::string_view str) {
        len = int32_t(str.size());
        int32_t buffered = len + 6;
        data = new char[buffered];
        std::memcpy(data, str.data(), len);
        // Zero-initialize buffered space after string
        for (size_t i = len; i < buffered; ++i) data[i] = 0;
    }

    void buffer::debug() {
        for (auto i = 0; i < std::min((size_t)100, tokens.size()); ++i) {
            int s = tokens[i];
            char c = data[s];
            std::println("{:4}: [{:9}] -> {}", i, s, c);
            if (c == '{' || c == '[') {
                s = tokens[tokens[++i]];
                std::println("{:4}: [{:9}] -> {} (jump)", i, s, c);
            } else if (c == '"' || c == '\'' || c == '-' ||
                                 c == '+' || (c >= '0' && c <= '9')) {
                s = tokens[++i];
                std::println("{:4}: [{:9}] -> {} (end)", i, s, c);
            }
        }
    }

    std::string buffer::debugToken(uint32_t i) const noexcept {
        int s = tokens[i];
        if (s < 0) {
            int t = -1 -s;
            s = tokens[t];
            char c = data[s];
            if (c == 0) c = ' ';
            return std::format("{:4}: [{:9}] -> [{:9}] -> {} (jump)", i, t, s, c);
        } else {
            char c = data[s];
            if (c == 0) c = ' ';
            return std::format("{:4}: [         ] -> [{:9}] -> {}", i, s, c);
        }
    }

    pointer buffer::begin() {
        return pointer(this, 0);
    }

    pointer buffer::end() {
        return pointer(this, int32_t(tokens.size()) - 1);
    }

    void buffer::tokenize() {
        int i = 0;
        char c;

        tokens.reserve(len / 4);
        std::vector<int32_t> skips;

        while ((c = data[i])) {
            switch (c) {
                break;case ' ':  case '\r':
                            case '\t': case '\n': case ',': {
                    while (WHITESPACE[data[++i]]);
                }
                break;case '/': {
                    c = data[++i];
                    if (c == '/') {
                        while ((c = data[++i]) != '\n' && c != '\r');
                        ++i;
                    } else {  // Assume *
                        c = data[++i];
                        do {
                            while (c != '*') c = data[++i];
                        } while ((c = data[++i]) != '/');
                    }
                }
                break;case '"':
                            case '\'': {
                    char q = c;
                    tokens.push_back(i);
                    while ((c = data[++i]) != q)
                        if (c == '\\') ++i;
                    tokens.push_back(i);
                    ++i;
                }
                break;case '{': case '[': {
                    tokens.push_back(i);
                    skips.push_back(int32_t(tokens.size()));
                    tokens.push_back(uint32_t(-1));
                    ++i;
                }
                break;case ']': case '}':
                    tokens[skips.back()] = uint32_t(tokens.size());
                    skips.pop_back();
                    tokens.push_back(i);
                    ++i;
                break;case ':':
                    tokens.push_back(i);
                    ++i;
                break;case 't': case 'n':
                    tokens.push_back(i);
                    i += 4;
                break;case 'f':
                    tokens.push_back(i);
                    i += 5;
                break;default: {
                    tokens.push_back(i);
                    while (!RAW_VALUE_END[data[++i]]);
                    tokens.push_back(i - 1);
                }
            }
        }

        tokens.push_back(len);
    }

    pointer::pointer(buffer *json, int32_t token): json(json), token(token) {}

    char pointer::tokenChar(int32_t _token) const noexcept {
        return json->data[json->tokens[_token]];
    }

    int32_t pointer::nextExpectedBinder(int32_t _token) const noexcept {
        switch (tokenChar(_token + 1)) {
            case '{': case '[': return json->tokens[_token + 2] + 3;
            case 't': case 'f': case 'n': return _token + 4;
            default: return _token + 5;
        }
    }

    pointer pointer::operator[](std::string_view key) {
        // TODO - Return Value Optimization
        if (tokenChar(token) != '{') return json->end();

        int _token = token + 4;
        while (tokenChar(_token) == ':') {
            int start = json->tokens[_token - 2] + 1;
            int end = json->tokens[_token - 1];
            if (key == std::string_view{&json->data[start], &json->data[end]}) {
                return pointer{json, _token + 1};
            }
            _token = nextExpectedBinder(_token);
        }

        return json->end();
    }

    std::string_view pointer::string() const {
        int start = json->tokens[token] + 1;
        int end = json->tokens[token + 1];
        return std::string_view{&json->data[start], &json->data[end]};
    }

    std::string_view pointer::key() const {
        int start = json->tokens[token - 3] + 1;
        int end = json->tokens[token - 2];
        return std::string_view{&json->data[start], &json->data[end]};
    }

    double pointer::f64() const {
        return std::strtod(&json->data[json->tokens[token]], nullptr);
    }


    double pointer::f64Fast() const {
        auto& data = json->data;
        auto i = json->tokens[token];
        auto c = data[i];

        int valueSign = 1;
        double value = 0;

        if (c == '-') {
            valueSign = -1;
            c = data[++i];
        } else if (c == '+') c = data[++i];

        if (c == 'N') return std::numeric_limits<double>::quiet_NaN();
        if (c == 'I') return valueSign == 1
            ? std::numeric_limits<double>::infinity()
            : -std::numeric_limits<double>::infinity();

        while (c >= '0' && c <= '9') {
            value = (value * 10) + (c - '0');
            c = data[++i];
        }

        if (c == '.') {
            double fractionWidth = 1;
            double fraction = 0;
            c = data[++i];
            while (c >= '0' && c <= '9') {
                fraction = (fraction * 10) + (c - '0');
                fractionWidth *= 10;
                c = data[++i];
            }

            value += (fraction / fractionWidth);
        }

        if (c == 'e' || c == 'E') {
            int exponent = 0;
            int exponentSign = 1;

            c = data[++i];
            if (c == '-') {
                exponentSign = -1;
                c = data[++i];
            } else if (c == '+') c = data[++i];
            while (c >= '0' && c <= '9') {
                exponent = (exponent * 10) + (c - '0');
                c = data[++i];
            }
            value *= valueSign * std::pow(10, exponentSign * exponent);
        } else {
            value *= valueSign;
        }

        if (i - 1 != json->tokens[token + 1])
            throw std::exception(std::format("Expected end of number [{}] got [{}]",
                json->tokens[token + 1], i - 1).c_str());

        return value;
    }

    std::string pointer::debug() const noexcept {
        int32_t t = json->tokens[token];
        char c = json->data[t];
        return std::format("token[{}]->str[{}]->'{}'", token, t, c);
    }

    // Iteration

    pointer pointer::next() const noexcept {
        int32_t next;
        if (tokenChar(token - 1) == ':') {
            next = nextExpectedBinder(token - 1);
            if (tokenChar(next) != ':') return json->end();
            ++next;
        } else {
            switch (tokenChar(token)) {
                case '{': case '[':
                    next = json->tokens[token + 1] + 1;
                break;case 't': case 'f': case 'n':
                    next = token + 1;
                break;default:
                    next = token + 2;
            }
            char c = tokenChar(next);
            if (c == ']' || c == '}') return json->end();
        }
        return pointer{json, next};
    }

    pointer pointer::begin() const noexcept {
        switch (tokenChar(token)) {
            case '{': {
                char c = tokenChar(token + 2);
                return c == '}' ? json->end() : pointer{json, token + 5};
            }
            case '[': {
                char c = tokenChar(token + 2);
                return c == ']' ? json->end() : pointer{json, token + 2};
            }
            default: return *this;
        }
    }

    pointer pointer::end() const noexcept {
        switch (tokenChar(token)) {
            case '{': case '[': case 0: return json->end();
            default: return next();
        }
    }

    pointer& pointer::operator++() noexcept {
        return *this = next();
    }

    bool pointer::operator==(const pointer& other) const noexcept {
        return token == other.token;
    }

    pointer pointer::operator*() const noexcept {
        return *this;
    }

    pointer::operator bool() const noexcept
    {
        return *this != json->end();
    }
}
