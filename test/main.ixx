module;

#include <print>

export module test;

import other;

export int main()
{
    std::println("{}", message());
}
