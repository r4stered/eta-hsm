// Tests for the exhaustive BFS backbone (the generic coverage engine over a
// machine table). The backbone enumerates every reachable (State, Event) of a
// machine, drives a live Machine to each State by replaying a reference-computed
// path, and asserts production dispatch agrees with the reference interpreter on
// the resting Leaf and the ordered Exit/Entry chain -- with no hand-authored
// expected transcript strings. It is generic over the table: the same harness runs
// on the flat cd_player, the deep nested machine, and an externally generated
// large machine with no machine-specific code.

#include "eta_hsm/reference/backbone.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/examples/example_control/example_control.hpp"
#include "eta_hsm/examples/nested/nested.hpp"
#include "eta_hsm/probe/scaling_generator.hpp"

namespace eta_hsm::reference {
namespace {

// Report every failure the backbone collected as a distinct GTest failure, so a
// divergence names the exact (State, Event) and facet that disagreed.
void expectClean(const BackboneReport& rep)
{
    for (auto const& f : rep.failures)
    {
        ADD_FAILURE() << f;
    }
}

// True if any failure message contains `needle` -- used to assert a planted fault
// trips the expected invariant.
bool anyFailureContains(const std::vector<std::string>& failures, std::string_view needle)
{
    for (auto const& f : failures)
    {
        if (f.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// The centerpiece on the deep hierarchy: for every reachable (State, Event) of the
// nested machine, production dispatch agrees with the reference -- resting Leaf and
// ordered Exit/Entry chain -- across the whole reachable space. This exercises the
// reference interpreter's cross-level Transitions, LCA, Local vs External, and the
// Top-sourced re-entry, since the nested machine uses all of them.
TEST(Backbone, NestedProductionAgreesWithReference)
{
    auto const rep = runBackbone<examples::nested::model>();
    expectClean(rep);
    EXPECT_TRUE(rep.ok());
    EXPECT_GT(rep.pairsVerified, 0u);
    EXPECT_EQ(rep.pairsVerified, rep.reachableLeaves * rep.eventCount);
}

// The very same harness, no machine-specific code, runs on the flat cd_player and
// agrees across its reachable space -- production vs reference on resting Leaf, the
// Exit/Entry chain, AND (cd_player's Player records a log) the full Action-bearing
// transcript. The flat machine has five reachable Leaves.
TEST(Backbone, CdPlayerProductionAgreesWithReference)
{
    auto const rep = runBackbone<examples::cd_player::player>();
    expectClean(rep);
    EXPECT_TRUE(rep.ok());
    EXPECT_EQ(rep.reachableLeaves, 5u);
    EXPECT_EQ(rep.pairsVerified, rep.reachableLeaves * rep.eventCount);
}

// The deep-hierarchy machine, whose Leaf States nest two levels under Top and whose
// Sober/Drunk Leaves declare During hooks, also agrees across its reachable space.
// Running a machine with real During hooks makes the During-inertness invariant
// non-vacuous: a tick metabolizes BAC without firing any Transition.
TEST(Backbone, ExampleControlProductionAgreesWithReference)
{
    auto const rep = runBackbone<examples::example_control::drinker>();
    expectClean(rep);
    EXPECT_TRUE(rep.ok());
    EXPECT_GT(rep.pairsVerified, 0u);
    EXPECT_EQ(rep.pairsVerified, rep.reachableLeaves * rep.eventCount);
}

// The guardHost parameter steers Guard outcomes: with the drawer jammed on the
// supplied Host, Hammer takes the guarded Transition (-> Open) instead of deferring
// to Top, so the guarded branch is now part of the reachable space and is verified.
TEST(Backbone, CdPlayerGuardedBranchAgreesUnderJammedHost)
{
    examples::cd_player::Player jammed{};
    jammed.drawer_stuck = true;
    auto const rep = runBackbone<examples::cd_player::player>(jammed);
    expectClean(rep);
    EXPECT_TRUE(rep.ok());
    EXPECT_GT(rep.pairsVerified, 0u);
}

// The harness accepts an externally-supplied table as its subject: a machine from
// the compile-time generator, with an empty Host (no log), is fed straight in and
// verified at scale -- structural Exit/Entry agreement plus all invariants, with no
// machine-specific code. This checks *correctness* on the same machines the scaling
// probe measures for compile *speed*.
TEST(Backbone, GeneratedMachineProductionAgreesWithReference)
{
    auto const rep = runBackbone<probe::generate<20, 3>()>();
    expectClean(rep);
    EXPECT_TRUE(rep.ok());
    EXPECT_GT(rep.pairsVerified, 0u);
    EXPECT_EQ(rep.pairsVerified, rep.reachableLeaves * rep.eventCount);
}

// Fault injection proves the invariant checks bite: a faithfully observed step
// passes every invariant, and corrupting one facet of that observation trips
// exactly the corresponding invariant. This exercises the pure invariantFailures
// check directly, so the safety net is shown to catch real violations -- not just
// to pass on a correct machine.
TEST(Backbone, InvariantsCatchPlantedFaults)
{
    using examples::nested::Event;
    using examples::nested::model;
    using examples::nested::State;

    auto const view = makeTableView<model>();
    std::size_t const budget = 2 * view.states.size();
    examples::nested::Model const guardHost{};

    // A faithfully observed real step (A1 + Go) violates no invariant.
    Reached<State, Event> const fromInitial{State::A1, {}};
    auto const good = observeStep<model>(view, fromInitial, Event::Go, guardHost);
    ASSERT_TRUE(good.replayLanded);
    EXPECT_TRUE(invariantFailures(view, good, budget).empty());

    // (6) During inertness: a During tick that moved the Leaf is caught.
    {
        auto bad = good;
        bad.afterDuringLeaf = State::A1;  // good.prodLeaf is B1b, so this is a move
        EXPECT_TRUE(anyFailureContains(invariantFailures(view, bad, budget), "During tick changed"));
    }
    // (6) A During tick that ran an Exit/Entry is caught.
    {
        auto bad = good;
        bad.duringActivity = 1;
        EXPECT_TRUE(anyFailureContains(invariantFailures(view, bad, budget), "During tick ran"));
    }
    // (1) Coming to rest in a non-Leaf (Composite) State is caught.
    {
        auto bad = good;
        bad.prodLeaf = State::B;  // Composite
        EXPECT_TRUE(anyFailureContains(invariantFailures(view, bad, budget), "non-Leaf"));
    }
    // (8) A corrupted Exit chain disagrees with the reference plan.
    {
        auto bad = good;
        bad.prodExits.clear();
        EXPECT_TRUE(anyFailureContains(invariantFailures(view, bad, budget), "Exit sequence mismatch"));
    }
    // (9) A divergent second run is caught as non-determinism.
    {
        auto bad = good;
        bad.prodLeaf2 = State::A1;  // differs from prodLeaf (B1b)
        EXPECT_TRUE(anyFailureContains(invariantFailures(view, bad, budget), "non-deterministic"));
    }
    // (5) An isInSubstateOf answer inconsistent with the active path is caught.
    {
        auto bad = good;
        ASSERT_FALSE(bad.substateOf.empty());
        bad.substateOf.front().second = !bad.substateOf.front().second;
        EXPECT_TRUE(anyFailureContains(invariantFailures(view, bad, budget), "isInSubstateOf"));
    }
    // (4) An unhandled Event that was not a strict no-op is caught.
    {
        // Within is declared only on B1a; from A1 it is unhandled (a strict no-op).
        auto unhandled = observeStep<model>(view, fromInitial, Event::Within, guardHost);
        ASSERT_FALSE(unhandled.refMatched);
        EXPECT_TRUE(invariantFailures(view, unhandled, budget).empty());
        unhandled.prodLeaf = State::B1b;  // a real move where there should be none
        EXPECT_TRUE(anyFailureContains(invariantFailures(view, unhandled, budget), "unhandled Event changed"));
    }
}

}  // namespace
}  // namespace eta_hsm::reference
