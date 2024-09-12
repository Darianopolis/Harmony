module;

#include <print>

export module test;

import other;
import std;
import <print>;

export int main()
{
    std::println("{}", message());
}
