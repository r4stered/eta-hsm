// Runtime random-machine fuzzing: generate arbitrary valid machine tables as
// runtime data and run the reference interpreter over machine shapes nobody would
// hand-write, asserting the universal HSM invariants and well-formedness hold.
//
// Ceiling: this fuzzes the semantic model (the reference interpreter) and the
// table's well-formedness, NOT the production `template for` dispatch -- a
// runtime-generated table cannot instantiate the NTTP `Machine<Table>`. A passing
// run here is NOT "the engine was fuzzed"; it hardens the interpreter and the
// shape of a valid table only.

#include "eta_hsm/reference/random_machine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>

#include "eta_hsm/reference/backbone.hpp"  // isLeaf
#include "eta_hsm/reference/reference_interpreter.hpp"

namespace eta_hsm::reference {
namespace {

using View = TableView<ProbeState, ProbeEvent, ProbeHost>;

// How wide the invariant sweeps fuzz: every reachable single step of every
// generated machine across this many seeds is checked.
constexpr std::uint64_t kSeeds = 300;
constexpr std::size_t kStates = 10;

// Visit every reachable (resting Leaf, Event) single step of `view`, invoking
// `fn(start, event, plan)` -- the unit the universal invariants are asserted over.
template <class Fn>
void forEachReachableStep(const View& view, Fn fn)
{
    ProbeHost const host{};
    constexpr auto events = eta_hsm::enum_values<ProbeEvent>();
    for (auto const& r : reachable(view, host))
    {
        for (ProbeEvent e : events)
        {
            fn(r.leaf, e, referenceStep(view, r.leaf, e, host));
        }
    }
}

// Tracer bullet: a machine generated from a fixed seed materializes to a runtime
// view whose initial resting Leaf is a declared Leaf State -- proving the recipe
// generator, the view materialization, and the interpreter's initial-Leaf drill
// line up end-to-end.
TEST(RandomMachine, GeneratedMachineRestsInDeclaredLeaf)
{
    auto const recipe = makeRecipe(/*seed=*/0u, /*stateCount=*/6u);
    auto const view = recipeToView(recipe);
    auto const leaf = initialLeaf(view);
    EXPECT_TRUE(isLeaf(view, leaf));
}

// A generated machine's Transitions actually move it: at least one seed reaches
// more than its initial resting Leaf. Without this the invariant sweeps below
// would pass vacuously on a machine that can never leave its initial Leaf.
TEST(RandomMachine, GeneratedTransitionsReachBeyondInitialLeaf)
{
    bool exploredSomewhere = false;
    for (std::uint64_t seed = 0; seed < 64 && !exploredSomewhere; ++seed)
    {
        auto const view = recipeToView(makeRecipe(seed, /*stateCount=*/8u));
        if (reachable(view, ProbeHost{}).size() > 1)
        {
            exploredSomewhere = true;
        }
    }
    EXPECT_TRUE(exploredSomewhere);
}

// Invariant 1: the machine only ever comes to rest in a Leaf. Every reachable
// resting Leaf, and every Leaf a matched step settles in, is a declared Leaf.
TEST(RandomMachine, Invariant1RestsInALeaf)
{
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed)
    {
        auto const view = recipeToView(makeRecipe(seed, kStates));
        forEachReachableStep(view, [&](ProbeState start, ProbeEvent e, const auto& plan) {
            EXPECT_TRUE(isLeaf(view, start)) << "seed " << seed << " " << stepLabel(start, e);
            if (plan.matched)
            {
                EXPECT_TRUE(isLeaf(view, plan.leaf)) << "seed " << seed << " " << stepLabel(start, e);
            }
        });
    }
}

// Invariant 2: a single step terminates under a bounded amount of Exit/Entry work.
// Each side of the chain visits a State at most once, so the total cannot exceed
// twice the State count; a runaway chain would blow past it. (The sweep returning
// at all also demonstrates the reachability walk itself terminates.)
TEST(RandomMachine, Invariant2StepTerminatesWithinBudget)
{
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed)
    {
        auto const view = recipeToView(makeRecipe(seed, kStates));
        std::size_t const budget = 2 * view.states.size();
        forEachReachableStep(view, [&](ProbeState start, ProbeEvent e, const auto& plan) {
            EXPECT_LE(plan.exits.size() + plan.entries.size(), budget) << "seed " << seed << " " << stepLabel(start, e);
        });
    }
}

// Invariant 4: an Event no State in the active chain handles is a strict no-op --
// no resting-Leaf change and no Exit/Entry.
TEST(RandomMachine, Invariant4UnhandledEventIsStrictNoOp)
{
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed)
    {
        auto const view = recipeToView(makeRecipe(seed, kStates));
        forEachReachableStep(view, [&](ProbeState start, ProbeEvent e, const auto& plan) {
            if (!plan.matched)
            {
                EXPECT_EQ(plan.leaf, start) << "seed " << seed << " " << stepLabel(start, e);
                EXPECT_TRUE(plan.exits.empty()) << "seed " << seed << " " << stepLabel(start, e);
                EXPECT_TRUE(plan.entries.empty()) << "seed " << seed << " " << stepLabel(start, e);
            }
        });
    }
}

// Invariant 7: the active configuration is one root-to-leaf chain (no cycle, no
// forest) -- both at every reachable resting Leaf and after every matched step.
TEST(RandomMachine, Invariant7ActivePathIsSingleRootChain)
{
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed)
    {
        auto const view = recipeToView(makeRecipe(seed, kStates));
        forEachReachableStep(view, [&](ProbeState start, ProbeEvent e, const auto& plan) {
            EXPECT_TRUE(isSingleRootChain(view, start)) << "seed " << seed << " " << stepLabel(start, e);
            if (plan.matched)
            {
                EXPECT_TRUE(isSingleRootChain(view, plan.leaf)) << "seed " << seed << " " << stepLabel(start, e);
            }
        });
    }
}

// Well-formedness: every generated machine is a valid table -- one Top, declared
// parents and Transition endpoints, child Initial Substates, no ambiguous
// (Source, Event) pair, every State anchored to Top.
TEST(RandomMachine, GeneratedMachinesAreWellFormed)
{
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed)
    {
        auto const view = recipeToView(makeRecipe(seed, kStates));
        auto const wf = wellFormed(view);
        EXPECT_TRUE(wf.ok) << "seed " << seed << ": " << wf.error;
    }
}

// Testing the tester: a deliberately corrupted recipe must be caught by the
// well-formedness check. Each sub-case breaks one facet of a valid machine and
// asserts the check fails -- so a passing well-formedness sweep means something.
TEST(RandomMachine, WellFormednessCatchesCorruptions)
{
    // A second Top State.
    {
        auto recipe = makeRecipe(1u, kStates);
        recipe.states[1].isTop = true;
        EXPECT_FALSE(wellFormed(recipeToView(recipe)).ok);
    }
    // An Initial Substate that is not a child (a State pointing at itself).
    {
        auto recipe = makeRecipe(2u, kStates);
        recipe.states[1].hasInitial = true;
        recipe.states[1].initial = recipe.states[1].index;
        EXPECT_FALSE(wellFormed(recipeToView(recipe)).ok);
    }
    // A Transition target that is not a declared State (an enumerator past the
    // declared prefix).
    {
        auto recipe = makeRecipe(3u, kStates);
        ASSERT_FALSE(recipe.transitions.empty());
        recipe.transitions[0].target = kStates + 5;
        EXPECT_FALSE(wellFormed(recipeToView(recipe)).ok);
    }
    // Two Guard-free Transitions sharing a (Source, Event) -- an ambiguous dispatch.
    {
        auto recipe = makeRecipe(4u, kStates);
        ASSERT_FALSE(recipe.transitions.empty());
        recipe.transitions.push_back(recipe.transitions[0]);
        EXPECT_FALSE(wellFormed(recipeToView(recipe)).ok);
    }
    // A non-Top State whose parent is not a declared State.
    {
        auto recipe = makeRecipe(5u, kStates);
        recipe.states[1].parent = kStates + 5;
        EXPECT_FALSE(wellFormed(recipeToView(recipe)).ok);
    }
    // A Composite State (one with a child) that carries no Initial Substate.
    {
        auto recipe = makeRecipe(6u, kStates);
        bool corrupted = false;
        for (auto& s : recipe.states)
        {
            bool const composite = std::any_of(recipe.states.begin(), recipe.states.end(),
                                               [&](const RecipeState& c) { return !c.isTop && c.parent == s.index; });
            if (composite && !s.isTop)
            {
                s.hasInitial = false;
                corrupted = true;
                break;
            }
        }
        ASSERT_TRUE(corrupted);
        EXPECT_FALSE(wellFormed(recipeToView(recipe)).ok);
    }
}

// The broad acceptance sweep: many seeds across a range of State counts, every
// reachable single step of every generated machine checked against the universal
// invariants and the table against well-formedness. Reports the count fuzzed and
// states the codegen-blind-spot ceiling in the test output, so a green run is not
// mistaken for "the production dispatch was fuzzed".
TEST(RandomMachine, FuzzSweepHoldsInvariantsAndReportsCeiling)
{
    constexpr std::uint64_t kSweepSeeds = 2000;
    std::size_t machines = 0;
    std::size_t stepsVerified = 0;

    for (std::uint64_t seed = 0; seed < kSweepSeeds; ++seed)
    {
        std::size_t const n = 2 + (seed % 15);  // vary 2..16 States across the sweep
        auto const view = recipeToView(makeRecipe(seed, n));
        ASSERT_TRUE(wellFormed(view).ok) << "seed " << seed;

        std::size_t const budget = 2 * view.states.size();
        forEachReachableStep(view, [&](ProbeState start, ProbeEvent e, const auto& plan) {
            std::string const where = "seed " + std::to_string(seed) + " " + stepLabel(start, e);
            EXPECT_TRUE(isLeaf(view, start)) << where;  // (1)
            EXPECT_TRUE(isSingleRootChain(view, start)) << where;  // (7)
            EXPECT_LE(plan.exits.size() + plan.entries.size(), budget) << where;  // (2)
            if (plan.matched)
            {
                EXPECT_TRUE(isLeaf(view, plan.leaf)) << where;  // (1)
                EXPECT_TRUE(isSingleRootChain(view, plan.leaf)) << where;  // (7)
            }
            else
            {
                EXPECT_EQ(plan.leaf, start) << where;  // (4)
                EXPECT_TRUE(plan.exits.empty() && plan.entries.empty()) << where;  // (4)
            }
            ++stepsVerified;
        });
        ++machines;
    }

    RecordProperty("machines", static_cast<int>(machines));
    RecordProperty("reachable_steps_verified", static_cast<int>(stepsVerified));
    std::cout << "[ random_machine ] fuzzed " << machines << " random machines, " << stepsVerified
              << " reachable single steps -- all invariants + well-formedness hold.\n"
              << "[ random_machine ] CEILING: this fuzzed the runtime reference interpreter, NOT the "
                 "production `template for` dispatch -- a runtime-generated table cannot instantiate "
                 "Machine<Table>. It hardens the semantic model and table well-formedness only.\n";
}

}  // namespace
}  // namespace eta_hsm::reference
