// Codegen fuzzing: materialize random machine recipes (random_machine.hpp) into
// real compile-time `Machine<Table>` instances and drive them through the
// exhaustive backbone. Unlike the runtime reference fuzzing (random_machine_test),
// this instantiates the production `template for` dispatch, so it fuzzes the real
// codegen on arbitrary machine shapes the reference interpreter alone cannot reach.

#include "eta_hsm/reference/random_codegen.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>

#include "eta_hsm/machine/validator.hpp"
#include "eta_hsm/reference/backbone.hpp"
#include "eta_hsm/reference/random_machine.hpp"
#include "eta_hsm/reference/reference_interpreter.hpp"

namespace eta_hsm::reference {
namespace {

// Tracer bullet: a production Machine instantiated from a seed comes to rest in the
// same initial Leaf the reference interpreter computes for the same seed -- proving
// the recipe -> production-table -> live-Machine path lines up with the recipe ->
// runtime-view path end to end.
TEST(RandomCodegen, ProductionMachineRestsInReferenceInitialLeaf)
{
    constexpr std::uint64_t seed = 0;
    constexpr std::size_t n = 6;

    auto const view = recipeToView(makeRecipe(seed, n));
    ProbeState const refLeaf = initialLeaf(view);

    Machine<randomTable<seed, n>()> m;
    EXPECT_EQ(m.identify(), refLeaf);
}

// A generated production table passes every production validator check -- the random
// recipe materializes into a table the real core accepts, across a span of seeds and
// State counts. Asserted at compile time (validate is consteval), so a malformed
// generated table would be a build error, not just a runtime expectation.
TEST(RandomCodegen, GeneratedTablesPassTheProductionValidator)
{
    static_assert(validate<randomTable<0u, 2u>()>().ok);  // smallest: Top + one Leaf
    static_assert(validate<randomTable<0u, 6u>()>().ok);
    static_assert(validate<randomTable<1u, 10u>()>().ok);
    static_assert(validate<randomTable<7u, 12u>()>().ok);
    static_assert(validate<randomTable<42u, 16u>()>().ok);

    EXPECT_TRUE((validate<randomTable<1u, 10u>()>().ok));
    EXPECT_TRUE((validate<randomTable<42u, 16u>()>().ok));
}

// Assert the production table and the runtime reference view describe the same
// machine for one (Seed, N): the table built through the `Hsm{}` builder
// (recipeToTable, read back via makeTableView) carries the identical States and
// Transitions the reference materialization (recipeToView) does.
template <std::uint64_t Seed, std::size_t N>
void expectTableMatchesView()
{
    auto const refView = recipeToView(makeRecipe(Seed, N));
    auto const prodView = makeTableView<randomTable<Seed, N>()>();
    std::string const where = "seed " + std::to_string(Seed) + ", " + std::to_string(N) + " States";

    ASSERT_EQ(prodView.states.size(), refView.states.size()) << where;
    for (std::size_t i = 0; i < refView.states.size(); ++i)
    {
        auto const& a = prodView.states[i];
        auto const& b = refView.states[i];
        EXPECT_EQ(a.state, b.state) << where << " state row " << i;
        EXPECT_EQ(a.parent, b.parent) << where << " state row " << i;
        EXPECT_EQ(a.isTop, b.isTop) << where << " state row " << i;
        EXPECT_EQ(a.hasInitial, b.hasInitial) << where << " state row " << i;
        if (a.hasInitial)
        {
            EXPECT_EQ(a.initial, b.initial) << where << " state row " << i;
        }
    }

    ASSERT_EQ(prodView.transitions.size(), refView.transitions.size()) << where;
    for (std::size_t i = 0; i < refView.transitions.size(); ++i)
    {
        auto const& a = prodView.transitions[i];
        auto const& b = refView.transitions[i];
        EXPECT_EQ(a.source, b.source) << where << " transition " << i;
        EXPECT_EQ(a.event, b.event) << where << " transition " << i;
        EXPECT_EQ(a.target, b.target) << where << " transition " << i;
        EXPECT_EQ(a.internal, b.internal) << where << " transition " << i;
        EXPECT_EQ(a.local, b.local) << where << " transition " << i;
    }
}

// The production table and the runtime reference view describe the *same* machine,
// across shape extremes: the smallest (Top + one Leaf), a mid-size nested machine,
// and one at the sweep's State ceiling. This is what lets a backbone run over the
// production table be trusted as fuzzing the same machine the reference interprets
// -- the two halves of the differential share one subject.
TEST(RandomCodegen, ProductionTableMatchesReferenceView)
{
    expectTableMatchesView<0u, 2u>();  // flat: Top + one Leaf
    expectTableMatchesView<3u, 12u>();  // mid-size, nested
    expectTableMatchesView<42u, 16u>();  // at the sweep's State ceiling
}

// Drive a real production `Machine<Table>` built from a random recipe through the
// exhaustive backbone. For every reachable (Leaf, Event) the
// backbone diffs the production `template for` dispatch against the independent
// reference and asserts the universal invariants. A green report means the
// production codegen agrees with the reference over this random machine -- the real
// dispatcher fuzzed, not just the semantic model. Non-vacuous: at least one
// reachable (Leaf, Event) pair is verified.
TEST(RandomCodegen, BackboneFuzzesProductionDispatchOnARandomMachine)
{
    auto const report = runBackbone<randomTable<11u, 12u>()>();

    EXPECT_TRUE(report.ok());
    for (auto const& f : report.failures)
    {
        ADD_FAILURE() << f;
    }
    EXPECT_GT(report.pairsVerified, 0u);
}

// How many distinct production machines the compile-time sweep fuzzes. Each is a
// separate `Machine<Table>` instantiation (distinct codegen), so the count is a
// build-cost knob, not a runtime loop bound -- the bound is logged below, never
// silently truncated. Unbounded fuzzing (thousands of machines) is a per-machine
// build loop's job; this sweep fuzzes a fixed corpus.
constexpr std::size_t kSweepMachines = 24;

// The State count for the machine named by `Seed`: varies 2..16 across the sweep so
// the corpus spans flat and deep shapes (the same spread the runtime sweep uses).
constexpr std::size_t sweepStateCount(std::uint64_t seed) { return 2 + (seed % 15); }

// Fuzz one machine of the sweep: build the production table for `Seed`, run the
// exhaustive backbone, and fail with the seed attached on any divergence/invariant
// violation. Accumulates the machine and reachable-pair counts for the report.
template <std::uint64_t Seed>
void fuzzOne(std::size_t& machines, std::size_t& pairs)
{
    auto const report = runBackbone<randomTable<Seed, sweepStateCount(Seed)>()>();
    EXPECT_TRUE(report.ok()) << "seed " << Seed;
    for (auto const& f : report.failures)
    {
        ADD_FAILURE() << "seed " << Seed << ": " << f;
    }
    ++machines;
    pairs += report.pairsVerified;
}

template <std::size_t... Is>
void fuzzSweep(std::index_sequence<Is...> /*seeds*/, std::size_t& machines, std::size_t& pairs)
{
    (fuzzOne<Is>(machines, pairs), ...);
}

// The acceptance sweep: a fixed corpus of distinct production machines, each
// instantiated as a real `Machine<Table>` and driven through the exhaustive backbone
// -- production `template for` dispatch diffed against the reference and the
// universal invariants asserted, over machine shapes nobody would hand-write. Unlike
// the runtime sweep (random_machine_test), this instantiates and fuzzes the real
// codegen. The corpus size and the State-count bound are logged so a green run is
// never mistaken for unbounded coverage.
TEST(RandomCodegen, FuzzSweepDiffsProductionCodegenAgainstReference)
{
    std::size_t machines = 0;
    std::size_t pairs = 0;
    fuzzSweep(std::make_index_sequence<kSweepMachines>{}, machines, pairs);

    EXPECT_EQ(machines, kSweepMachines);
    EXPECT_GT(pairs, 0u);

    RecordProperty("production_machines", static_cast<int>(machines));
    RecordProperty("reachable_pairs_verified", static_cast<int>(pairs));
    std::cout << "[ random_codegen ] fuzzed " << machines << " distinct production machines (seeds 0.."
              << (kSweepMachines - 1) << ", 2..16 States, corpus capped at " << kSweepMachines << " by compile cost), "
              << pairs << " reachable (Leaf, Event) pairs -- production dispatch diffed against the reference.\n"
              << "[ random_codegen ] this instantiated the production `template for` dispatch via "
                 "Machine<Table> and fuzzed the real codegen, not only the runtime reference interpreter. "
                 "Unbounded fuzzing beyond this corpus is a per-machine build loop's job.\n";
}

}  // namespace
}  // namespace eta_hsm::reference
