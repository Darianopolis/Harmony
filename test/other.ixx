module;
#include "thing.hpp"
export module other;
export import std;

export const char* message()
{
    return MESSAGE;
}
