// Emit one shaped machine as a standalone C++ translation unit on stdout. Driven
// by the frontier harnesses (tools/tracer_past64.sh, tools/scaling_frontier.sh):
// `emit <shape> <N>` prints the source for an N-State machine of the named shape,
// which the harness then compiles with the cap raised (-DETA_HSM_MAX_STATES) and
// runs -- compiling the production `template for` dispatch and consteval validator
// on a machine far past the default capacity, one shape and size per compile.
//
// This is not a test (it asserts nothing); it is the source generator the
// harnesses shell out to. Correctness of what it emits is established by compiling
// and running each emitted TU through the exhaustive backbone.

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "eta_hsm/probe/shape_source.hpp"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: " << (argc > 0 ? argv[0] : "emit")
                  << " <shape> <N>\n"
                     "  shape: deep_chain | wide_star | balanced_tree | dense_transitions\n";
        return 2;
    }

    auto const shape = eta_hsm::probe::parseShape(argv[1]);
    if (!shape)
    {
        std::cerr << "unknown shape: " << argv[1]
                  << " (want deep_chain | wide_star | balanced_tree | dense_transitions)\n";
        return 2;
    }
    auto const n = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));

    std::cout << eta_hsm::probe::shapeToSource(*shape, n);
    return 0;
}
