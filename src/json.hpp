#pragma once

#include <yyjson.h>

#ifndef HARMONY_USE_IMPORT_STD
#include <iterator>
#include <print>
#endif

struct yyjson_arr_iterator
{
    size_t idx, max;
    yyjson_val* val;

    yyjson_arr_iterator(yyjson_val* arr)
        : idx(0)
        , max(yyjson_arr_size(arr))
        , val(yyjson_arr_get_first(arr))
    {}

    using value_type = yyjson_val*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    auto& operator++() noexcept
    {
        idx++;
        val = unsafe_yyjson_get_next(val);
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

    auto operator*() const noexcept
    {
        return val;
    }
};

struct yyjson_arr_range
{
    yyjson_val* arr = nullptr;

    auto begin() noexcept
    {
        return yyjson_arr_iterator(arr);
    }

    auto end() noexcept
    {
        return std::default_sentinel;
    }
};
