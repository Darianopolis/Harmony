#include <build.hpp>
#include <configuration.hpp>

#include <backend/msvc/msvc-backend.hpp>

int main()
{
    cfg::Step step{
        .sources = {{
            .root = "src",
        }},
        .output = {{
            .output = "out/harmony",
        }},
    };

    MsvcBackend msvc;
    Build(step, msvc);
}
