// Emit one random machine as a standalone C++ translation unit on stdout. Driven
// by the build loop (tools/codegen_fuzz.sh): `emit <seed> <states>` prints the
// source for the machine named by (seed, states), which the loop then compiles and
// runs as its own translation unit -- fuzzing the production `template for` codegen
// on one random machine per compile, past a single TU's compile budget.
//
// This is not a test (it asserts nothing); it is the source generator the fuzz loop
// shells out to. Correctness of what it emits is established by compiling and
// running each emitted TU.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "eta_hsm/reference/random_source.hpp"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: " << (argc > 0 ? argv[0] : "emit") << " <seed> <states>\n";
        return 2;
    }

    auto const seed = static_cast<std::uint64_t>(std::strtoull(argv[1], nullptr, 10));
    auto const states = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));

    std::cout << eta_hsm::reference::recipeToSource(seed, states);
    return 0;
}
