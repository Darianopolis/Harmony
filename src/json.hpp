#pragma once

#include <yyjson.h>

#ifndef HARMONY_USE_IMPORT_STD
#include <iterator>
#include <print>
#include <utility>
#endif

struct JsonValue;

struct JsonIterator
{
    enum class Mode
    {
        none,
        array,
        object,
        optional
    };

    size_t idx = 0;
    size_t max = 0;
    yyjson_val* val = nullptr;
    const char* key;
    Mode mode = Mode::none;

    JsonIterator(yyjson_val* range, const char* _key)
        : key(_key)
    {
        if (yyjson_is_arr(range)) {
            max = yyjson_arr_size(range);
            val = yyjson_arr_get_first(range);
            mode = Mode::array;
        } else if (yyjson_is_obj(range)) {
            max = yyjson_obj_size(range);
            val = unsafe_yyjson_get_first(range);
            mode = Mode::object;
        } else if (range) {
            mode = Mode::optional;
            max = 1;
            val = range;
        }
    }

    using value_type = JsonValue;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    auto& operator++() noexcept
    {
        idx++;
        if (mode == Mode::object) {
            val = unsafe_yyjson_get_next(val + 1);
        } else if (mode == Mode::array) {
            val = unsafe_yyjson_get_next(val);
        }
        return *this;
    }

    auto& operator++(int) noexcept
    {
        auto old = *this;
        operator++();
        return old;
    }

    bool operator==(std::default_sentinel_t) const noexcept
    {
        return idx >= max;
    }

    struct JsonValue operator*() const noexcept;
};

struct JsonValue
{
    const char* key;
    yyjson_val* val;

    const char* string() const noexcept { return yyjson_get_str(val); }
    std::optional<uint64_t> uint64() const noexcept { if (yyjson_is_int(val)) return yyjson_get_int(val); return std::nullopt; }
    std::optional<int64_t> int64() const noexcept { if (yyjson_is_sint(val)) return yyjson_get_sint(val); return std::nullopt; }
    std::optional<double> real() const noexcept { if (yyjson_is_real(val)) return yyjson_get_real(val); return std::nullopt; }
    auto begin() const noexcept { return JsonIterator(val, key); }
    auto end() const noexcept { return std::default_sentinel; }
    bool obj() const noexcept { return yyjson_is_obj(val); }
    bool arr() const noexcept { return yyjson_is_arr(val); }
    operator bool() const noexcept { return val; }
    struct JsonValue operator[](const char* _key) const noexcept { return JsonValue(_key, yyjson_obj_get(val, _key)); }
    struct JsonValue operator[](int index) const noexcept { return JsonValue(nullptr, yyjson_arr_get(val, index)); }
    struct JsonValue operator[](uint64_t index) const noexcept { return JsonValue(nullptr, yyjson_arr_get(val, index)); }
    struct JsonValue operator[](int64_t index) const noexcept { return JsonValue(nullptr, yyjson_arr_get(val, index)); }
};

template<> struct std::tuple_size<JsonValue> { static constexpr size_t value = 2; };
template<> struct std::tuple_element<0, JsonValue> { using type = typename const char*; };
template<> struct std::tuple_element<1, JsonValue> { using type = typename JsonValue; };
namespace std {
    template<size_t Idx> auto get(JsonValue ptr) { if constexpr (Idx == 0) { return ptr.key; } else { return ptr; } }
}

inline JsonValue JsonIterator::operator*() const noexcept
{
    if (mode == Mode::object) {
        return JsonValue(yyjson_get_str(val), val + 1);
    } else if (mode == Mode::array) {
        return JsonValue({}, val);
    } else {
        return JsonValue(key, val);
    }
}

struct JsonDocument
{
    yyjson_doc* doc = nullptr;

    JsonDocument(std::string_view json)
        : doc(yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS))
    {
        if (!doc) Error("Error parsing json");
    }

    ~JsonDocument()
    {
        yyjson_doc_free(doc);
    }

    JsonValue root() const noexcept { return JsonValue(nullptr, yyjson_doc_get_root(doc)); }
};
