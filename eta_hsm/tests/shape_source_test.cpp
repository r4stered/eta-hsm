// Behavioral tests for the shaped-machine source emitter (shape_source.hpp).
//
// The emitter's job is to turn a (Shape, N) into a well-formed recipe and a
// self-contained C++ translation unit. These tests pin the recipe topology each
// shape is supposed to have (the property that makes it isolate its subsystem) and
// the structural shape of the emitted source -- the right enum widths, a builder
// chain, and the backbone driver. The end-to-end proof that the emitted source
// compiles and matches the reference past the default cap lives in the harnesses
// (tools/tracer_past64.sh, tools/scaling_frontier.sh); this is the fast unit-level
// counterpart that also keeps the header compiling under the strict warning set.

#include "eta_hsm/probe/shape_source.hpp"

#include <gtest/gtest.h>

#include <string>

namespace eta_hsm::probe {
namespace {

// The deep chain is a single spine: every node hangs off its immediate
// predecessor, so depth is N-1. That long Exit/Entry chain is the cost it isolates.
TEST(ShapeSource, DeepChainIsASpine)
{
    auto const r = makeShape(Shape::DeepChain, 10);
    EXPECT_EQ(r.n, 10u);
    for (std::size_t i = 1; i < r.n; ++i)
    {
        EXPECT_EQ(r.parent[i], i - 1) << "node " << i;
    }
}

// The wide star is depth 1: every non-Top State is a direct child of Top. The
// breadth is the cost it isolates.
TEST(ShapeSource, WideStarIsFlat)
{
    auto const r = makeShape(Shape::WideStar, 10);
    EXPECT_EQ(r.n, 10u);
    for (std::size_t i = 1; i < r.n; ++i)
    {
        EXPECT_EQ(r.parent[i], 0u) << "node " << i;
    }
}

// The balanced tree is a complete binary tree: node i's parent is (i-1)/2.
TEST(ShapeSource, BalancedTreeIsBinary)
{
    auto const r = makeShape(Shape::BalancedTree, 10);
    EXPECT_EQ(r.n, 10u);
    for (std::size_t i = 1; i < r.n; ++i)
    {
        EXPECT_EQ(r.parent[i], (i - 1) / 2) << "node " << i;
    }
}

// The dense-transitions shape carries a wide Event enum and a near-complete
// transition graph -- every non-Top State transitions on every Event -- so the
// table holds ~ (n-1) * eventCount Transitions. That quadratic mass is what
// stresses the validator's O(transitions^2) duplicate-pair scan.
TEST(ShapeSource, DenseTransitionsArePacked)
{
    auto const r = makeShape(Shape::DenseTransitions, 8);
    EXPECT_EQ(r.eventCount, 8u);  // Event enum scales with N
    EXPECT_EQ(r.transitions.size(), (r.n - 1) * r.eventCount);
}

// No two emitted Transitions share a (Source, Event) without distinct Guards, so
// every shape's recipe is a well-formed table the validator accepts. (The probe
// Host has no Guards, so any collision would be a duplicate.)
TEST(ShapeSource, NoDuplicateSourceEventPairs)
{
    for (Shape shape :
         {Shape::DeepChain, Shape::WideStar, Shape::BalancedTree, Shape::DenseTransitions, Shape::WorstCase})
    {
        auto const r = makeShape(shape, 12);
        for (std::size_t i = 0; i < r.transitions.size(); ++i)
        {
            for (std::size_t j = i + 1; j < r.transitions.size(); ++j)
            {
                bool const collide =
                    r.transitions[i].src == r.transitions[j].src && r.transitions[i].event == r.transitions[j].event;
                EXPECT_FALSE(collide) << shapeName(shape) << " rows " << i << "," << j;
            }
        }
    }
}

// The worst case is a dense comb: a deep spine (odd indices, each under the
// previous odd node) with a Leaf hanging off every rung (even indices, under the
// spine node before them), and a transition graph as dense as dense_transitions.
// It is simultaneously deep, bushy, and quadratically transitioned.
TEST(ShapeSource, WorstCaseIsADenseComb)
{
    auto const r = makeShape(Shape::WorstCase, 10);
    EXPECT_EQ(r.eventCount, 10u);  // wide Event enum, like dense_transitions
    EXPECT_EQ(r.transitions.size(), (r.n - 1) * r.eventCount);  // quadratically dense
    // Spine: node 1 under Top, each later odd node under the previous odd node.
    EXPECT_EQ(r.parent[1], 0u);
    EXPECT_EQ(r.parent[3], 1u);
    EXPECT_EQ(r.parent[5], 3u);
    // Hanging Leaves: each even node hangs off the spine node just before it.
    EXPECT_EQ(r.parent[2], 1u);
    EXPECT_EQ(r.parent[4], 3u);
    // Initials drill straight down the spine, so the resting config is maximally deep.
    EXPECT_TRUE(r.hasInitial[1]);
    EXPECT_EQ(r.initial[1], 3u);
}

// The emitted source declares exactly N State enumerators (so every enumerator is a
// declared State and no .unwired is needed), the matching Event enum, and drives
// the machine through the exhaustive backbone.
TEST(ShapeSource, EmittedSourceHasEnumsAndBackbone)
{
    std::string const src = shapeToSource(Shape::BalancedTree, 5);
    EXPECT_NE(src.find("enum class GenState"), std::string::npos);
    EXPECT_NE(src.find("enum class GenEvent"), std::string::npos);
    EXPECT_NE(src.find("    S4,\n"), std::string::npos);  // the Nth (last) State enumerator
    EXPECT_EQ(src.find("    S5,\n"), std::string::npos);  // and no more than N
    EXPECT_NE(src.find("Hsm<GenHost, GenState, GenEvent>"), std::string::npos);  // the builder chain
    EXPECT_NE(src.find("runBackbone<kShapedTable>"), std::string::npos);  // drives a real Machine<Table>
}

// Shape names round-trip through parseShape, and an unknown name is rejected.
TEST(ShapeSource, ShapeNamesRoundTrip)
{
    for (Shape shape :
         {Shape::DeepChain, Shape::WideStar, Shape::BalancedTree, Shape::DenseTransitions, Shape::WorstCase})
    {
        auto const parsed = parseShape(shapeName(shape));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, shape);
    }
    EXPECT_FALSE(parseShape("nonsense").has_value());
}

}  // namespace
}  // namespace eta_hsm::probe
