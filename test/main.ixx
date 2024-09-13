export module test;

import other;
import std;

export
int main()
{
    std::println("{}", message());

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist{0.f, 1.f};
    std::println("value = {}", dist(rng));
}
