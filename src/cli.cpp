#include <build.hpp>
#include <configuration.hpp>

#include <backend/msvc/msvc-backend.hpp>

int main()
{
    cfg::Step step{
        .sources = {{
            .root = "test",
        }},
        .output = {{
            .output = "out/test",
        }},
    };

    MsvcBackend msvc;
    Build(step, msvc);
}
