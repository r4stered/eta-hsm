// Behavioral tests for the State hierarchy, driven through the
// public interface (dispatch / identify / isInSubstateOf) against a multi-level
// machine with three Composite States (A, B, B1). They assert the resting Leaf
// State and the exact ordered Exit/Entry/Action chain a Transition runs -- never
// internal table layout or generated-dispatch structure.

#include "eta_hsm/examples/nested/nested.hpp"

#include <gtest/gtest.h>

#include "eta_hsm/machine/machine.hpp"

namespace eta_hsm::examples::nested {
namespace {

// Entering a Composite State settles in its Initial Substate, recursively: at
// construction the machine drills Top -> A -> A1, firing Entry for each in order.
TEST(Nested, ConstructionDrillsToDeepestLeaf)
{
    Machine<model> m;
    EXPECT_EQ(m.identify(), State::A1);
    EXPECT_EQ(m.host().log, "+Top;+A;+A1;");
}

// A cross-level Transition runs the ordered chain: Exit from the Source Leaf up
// to (but not including) the least-common-ancestor, then the Action, then Entry
// down to the Target. A1 -> B1b crosses from the A branch into the B branch under
// the common ancestor Top: exit A1, A; run go; enter B, B1, B1b.
TEST(Nested, CrossLevelTransitionRunsOrderedChain)
{
    Machine<model> m;
    m.host().log.clear();
    m.dispatch(Event::Go);
    EXPECT_EQ(m.identify(), State::B1b);
    EXPECT_EQ(m.host().log, "-A1;-A;go;+B;+B1;+B1b;");
}

// A Transition whose Target is a Composite State settles in that State's Initial
// Substate, recursively. A1 -> B targets the Composite B, which drills B -> B1 ->
// B1a; the chain enters B, B1, B1a in order and rests in the Leaf B1a.
TEST(Nested, CompositeTargetDrillsToInitialSubstate)
{
    Machine<model> m;
    m.host().log.clear();
    m.dispatch(Event::ToB);
    EXPECT_EQ(m.identify(), State::B1a);
    EXPECT_EQ(m.host().log, "-A1;-A;+B;+B1;+B1a;");
}

// A cross-level Transition inside one Composite branch exits only as far as their
// shared ancestor. B1a -> B2 share the ancestor B: exit B1a, B1, then enter B2 --
// B is neither exited nor re-entered.
TEST(Nested, CrossLevelTransitionWithinComposite)
{
    Machine<model> m;
    m.dispatch(Event::ToB);  // rest in B1a
    m.host().log.clear();
    m.dispatch(Event::Up);
    EXPECT_EQ(m.identify(), State::B2);
    EXPECT_EQ(m.host().log, "-B1a;-B1;+B2;");
}

// An Event no State on the active path handles itself defers to the nearest
// ancestor that does. Reset is declared only on Top; from the deep Leaf B1a it
// exits B1a, B1, B, then -- as a Top-sourced External Transition -- exits and
// re-enters Top before drilling back into A -> A1.
TEST(Nested, EventDefersToNearestAncestorHandler)
{
    Machine<model> m;
    m.dispatch(Event::ToB);  // rest in B1a
    m.host().log.clear();
    m.dispatch(Event::Reset);
    EXPECT_EQ(m.identify(), State::A1);
    EXPECT_EQ(m.host().log, "-B1a;-B1;-B;-Top;+Top;+A;+A1;");
}

// External semantics (the default) on a parent/child Transition exit and re-enter
// the shared ancestor. A -> A1 declared with .on: from A1 it exits A1 and A, then
// re-enters A and A1.
TEST(Nested, ExternalParentChildReentersAncestor)
{
    Machine<model> m;
    m.host().log.clear();
    m.dispatch(Event::ReenterExt);
    EXPECT_EQ(m.identify(), State::A1);
    EXPECT_EQ(m.host().log, "-A1;-A;+A;+A1;");
}

// Local semantics on the same parent/child Transition do NOT exit or re-enter the
// shared ancestor. A -> A1 declared with .local: from A1 it exits and re-enters
// only A1; A stays active throughout. This is the sole difference from External.
TEST(Nested, LocalParentChildKeepsAncestor)
{
    Machine<model> m;
    m.host().log.clear();
    m.dispatch(Event::ReenterLoc);
    EXPECT_EQ(m.identify(), State::A1);
    EXPECT_EQ(m.host().log, "-A1;+A1;");
}

// isInSubstateOf reports nested ancestry: a deep Leaf is a substate of every
// Composite State above it, and not of unrelated branches.
TEST(Nested, IsInSubstateOfReportsNestedAncestry)
{
    Machine<model> m;
    m.dispatch(Event::ToB);  // rest in B1a
    EXPECT_TRUE(m.isInSubstateOf(State::B1a));  // the Leaf itself
    EXPECT_TRUE(m.isInSubstateOf(State::B1));  // its Composite parent
    EXPECT_TRUE(m.isInSubstateOf(State::B));  // its Composite grandparent
    EXPECT_TRUE(m.isInSubstateOf(State::Top));  // the root
    EXPECT_FALSE(m.isInSubstateOf(State::A));  // the other branch
    EXPECT_FALSE(m.isInSubstateOf(State::B2));  // a sibling under B
}

// Driving the machine the way a user would: assert the resting Leaf after every
// Dispatch across the full hierarchy.
TEST(Nested, ScriptedRunProducesExpectedStateSequence)
{
    Machine<model> m;
    ASSERT_EQ(m.identify(), State::A1);

    struct Step {
        Event event;
        State expected;
    };
    constexpr Step script[] = {
        {Event::ToB, State::B1a},  // A1  -> B (drills to B1a)
        {Event::Within, State::B1b},  // B1a -> B1b (sibling)
        {Event::Up, State::B2},  // B1b -> ... defers? Up is on B1a only
    };
    // Up is declared on B1a, not B1b; from B1b it is unhandled, so the machine
    // stays in B1b. Assert that explicitly rather than encoding it in the script.
    m.dispatch(script[0].event);
    EXPECT_EQ(m.identify(), script[0].expected);
    m.dispatch(script[1].event);
    EXPECT_EQ(m.identify(), script[1].expected);
    m.dispatch(Event::Up);  // unhandled from B1b
    EXPECT_EQ(m.identify(), State::B1b);

    m.dispatch(Event::Back);  // B1b -> ... Back is on B1a only; unhandled from B1b
    EXPECT_EQ(m.identify(), State::B1b);

    m.dispatch(Event::Reset);  // Top handles Reset from anywhere -> A1
    EXPECT_EQ(m.identify(), State::A1);
    m.dispatch(Event::Go);  // A1 -> B1b
    EXPECT_EQ(m.identify(), State::B1b);
}

}  // namespace
}  // namespace eta_hsm::examples::nested
