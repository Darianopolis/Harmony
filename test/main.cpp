import other;

int main()
{
    std::println("{} {}", message(), suffix());

    std::vector foo { 1, 2, 3, 4, 5 };
    for (auto v : foo) {
        std::println(" - {}", v);
    }
}
