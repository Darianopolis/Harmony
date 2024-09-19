export module other;

#ifdef HARMONY_BUILD
import <thing.hpp>;
#else
#include "thing.hpp"
#endif

export const char* message()
{
    return MESSAGE;
}
