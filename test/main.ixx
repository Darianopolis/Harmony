module;

#ifndef HARMONY_BUILD
#include <print>
#include <random>
#include <vector>

#include "other.ixx"
#endif

export module test;

#ifdef HARMONY_BUILD
import other;
import std;
#endif

export
int main()
{
    std::println("{}", message());

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist{0.f, 1.f};
    std::println("value = {}", dist(rng));

    std::vector vowels {'A', 'E', 'I', 'O', 'U'};
    for (auto v : vowels) {
        std::println(" - {}", v);
    }
}
