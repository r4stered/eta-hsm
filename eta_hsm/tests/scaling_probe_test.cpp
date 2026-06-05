// Behavioral tests for the scaling-probe generator (issue 0012).
//
// The probe's deliverable is a *measurement* -- a compile-time/peak-memory curve
// against State count (tools/scaling_probe.sh). But the curve is only meaningful
// if the machines being timed are real, well-formed machines, so this is what the
// test pins: that generate<N, Depth>() emits a *valid* N-State machine that the
// existing Machine core accepts and can actually dispatch. These same large
// machines are reused by issue 0016 as correctness subjects, so "valid at scale"
// is the contract.
//
// generate() is consteval, so the structural properties (State count, validity)
// are asserted at compile time; the dispatch behavior is asserted at runtime.

#include <gtest/gtest.h>

#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/machine/validator.hpp"
#include "eta_hsm/probe/scaling_generator.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::probe {
namespace {

// generate<N, Depth>() declares exactly N States -- the knob the sweep turns.
TEST(ScalingProbe, EmitsExactlyNStates)
{
    static_assert(generate<7, 1>().stateCount == 7);
    static_assert(generate<45, 2>().stateCount == 45);
    static_assert(generate<60, 3>().stateCount == 60);
    EXPECT_EQ((generate<7, 1>().stateCount), 7u);
    EXPECT_EQ((generate<45, 2>().stateCount), 45u);
    EXPECT_EQ((generate<60, 3>().stateCount), 60u);
}

// Every generated machine across the sweep range and a span of depths is
// well-formed: it passes all six validator checks. This is what lets the harness
// time a real machine rather than a malformed table the core would reject.
TEST(ScalingProbe, ValidAcrossRangeAndDepth)
{
    static_assert(validate<generate<2, 1>()>().ok);  // smallest: Top + one Leaf
    static_assert(validate<generate<7, 1>()>().ok);  // the spike's size, flat
    static_assert(validate<generate<7, 2>()>().ok);  // the spike's size, nested
    static_assert(validate<generate<20, 3>()>().ok);
    static_assert(validate<generate<45, 2>()>().ok);  // a large machine's size
    static_assert(validate<generate<45, 4>()>().ok);  // a large machine's size, deeper
    static_assert(validate<generate<60, 3>()>().ok);  // a little beyond a large machine

    EXPECT_TRUE((validate<generate<45, 2>()>().ok));
    EXPECT_TRUE((validate<generate<60, 3>()>().ok));
}

// A Composite tree really forms when Depth > 1: at least one generated State
// nests below another (not every State is a direct child of Top). Depth == 1
// degenerates to a star, which the flat case above already covers.
TEST(ScalingProbe, DepthProducesNesting)
{
    constexpr auto table = generate<45, 3>();
    // Some State's parent is itself a non-Top State -> real hierarchy exists.
    bool nestedFound = false;
    for (std::size_t i = 0; i < table.stateCount; ++i)
    {
        const auto& row = table.states[i];
        if (row.isTop)
        {
            continue;
        }
        for (std::size_t j = 0; j < table.stateCount; ++j)
        {
            if (table.states[j].state == row.parent && !table.states[j].isTop)
            {
                nestedFound = true;
            }
        }
    }
    EXPECT_TRUE(nestedFound);
}

// A large machine instantiates and dispatches: it comes to rest in a
// Leaf, survives a sweep of every Event, and stays live. Proves the generated
// table drives the real core, not just that it validates on paper.
TEST(ScalingProbe, LargeMachineDispatches)
{
    Machine<generate<45, 2>()> machine;
    const ProbeState start = machine.identify();

    // The resting State is a declared Leaf of the generated table.
    EXPECT_LT(static_cast<int>(start), 45);

    // Dispatching every Event must never wedge the machine -- it always settles
    // in a valid Leaf (one of the 45 declared States).
    for (ProbeEvent event : enum_values<ProbeEvent>())
    {
        machine.dispatch(event);
        EXPECT_GE(static_cast<int>(machine.identify()), 0);
        EXPECT_LT(static_cast<int>(machine.identify()), 45);
    }
}

}  // namespace
}  // namespace eta_hsm::probe
