#pragma once

// shapeToSource: emit a structured large machine as a complete, standalone C++
// translation unit that declares its own State / Event enums, builds the
// production Hsm table with explicit builder calls, instantiates a real
// Machine<Table>, and drives it through the exhaustive backbone.
//
// Where random_source.hpp emits *random* machines over the fixed 64-enumerator
// ProbeState/ProbeEvent enums (so it cannot reach past the default cap), this emits the
// State and Event enumerators as source -- exactly N of them -- so a machine far
// larger than the default capacity can be compiled with the cap raised
// (-DETA_HSM_MAX_STATES). The enumerators must be spelled out as source because a
// State enum cannot be synthesized at compile time; emitting them is the whole
// point of this path.
//
// Four single-axis shapes each isolate a different subsystem of the toolchain, and
// a fifth stacks them into the pathological combination:
//   - DeepChain        depth ~ N-1: per-dispatch chain length, LCA, path[] depth,
//                      recursive Initial-Substate drilling.
//   - WideStar         depth 1: `template for` breadth and table size.
//   - BalancedTree     the realistic middle (a binary tree).
//   - DenseTransitions a wide Event enum packing a near-complete transition graph:
//                      the validator's O(transitions^2) ambiguity check.
//   - WorstCase        a "dense comb": a deep spine that also hangs a leaf off every
//                      rung (deep ~N/2 AND bushy ~N/2 leaves) under a wide Event enum
//                      with a near-complete transition graph. Worst on every axis at
//                      once -- the quadratic validator scan (compile wall) layered on
//                      the longest Exit/Entry chains and the largest reachable space
//                      (runtime-differential wall).
//
// The emitted program prints its verified-pair count and structural metrics and
// exits nonzero on any production-vs-reference divergence, invariant violation, or
// a vacuous run -- a self-contained reproducer for (shape, N).

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace eta_hsm::probe {

// The machine family to emit. Each isolates a different cost/behavior axis.
enum class Shape { DeepChain, WideStar, BalancedTree, DenseTransitions, WorstCase };

// The lowercase, underscored name of a shape (stable, used on the command line
// and in the emitted reproducer's banner).
inline std::string_view shapeName(Shape s)
{
    switch (s)
    {
        case Shape::DeepChain:
            return "deep_chain";
        case Shape::WideStar:
            return "wide_star";
        case Shape::BalancedTree:
            return "balanced_tree";
        case Shape::DenseTransitions:
            return "dense_transitions";
        case Shape::WorstCase:
            return "worst_case";
    }
    return "?";
}

// Parse a shape name back to a Shape (nullopt if unrecognized).
inline std::optional<Shape> parseShape(std::string_view name)
{
    if (name == "deep_chain")
    {
        return Shape::DeepChain;
    }
    if (name == "wide_star")
    {
        return Shape::WideStar;
    }
    if (name == "balanced_tree")
    {
        return Shape::BalancedTree;
    }
    if (name == "dense_transitions")
    {
        return Shape::DenseTransitions;
    }
    if (name == "worst_case")
    {
        return Shape::WorstCase;
    }
    return std::nullopt;
}

// A plain description of one shaped machine, by enumerator index. Index 0 is Top.
// `parent[i]` is the parent of non-Top State i; `hasInitial[i]` / `initial[i]`
// give a Composite State its Initial Substate. The transitions list is already
// free of duplicate (source, event) pairs, so the emitted table is well-formed.
struct ShapeRecipe {
    std::size_t n{0};  // total States, including Top
    std::size_t eventCount{0};
    std::vector<std::size_t> parent;  // size n; parent[0] is unused (Top)
    std::vector<bool> hasInitial;  // size n
    std::vector<std::size_t> initial;  // size n
    struct Tr {
        std::size_t src{0};
        std::size_t event{0};
        std::size_t tgt{0};
        bool local{false};
    };
    std::vector<Tr> transitions;
};

namespace detail {

// Per-State outgoing Transition count for the tree/chain/star shapes. Kept <=
// eventCount so each State's Transitions land on distinct Events (a unique
// (Source, Event) per State).
inline constexpr std::size_t kTransPerState = 3;
inline constexpr std::size_t kTreeEvents = 6;

// Give every Composite node (one that is some node's parent) its first declared
// child as Initial Substate, so the machine always drills to a Leaf.
inline void fillInitials(ShapeRecipe& r)
{
    for (std::size_t p = 0; p < r.n; ++p)
    {
        for (std::size_t c = 1; c < r.n; ++c)
        {
            if (r.parent[c] == p)
            {
                r.hasInitial[p] = true;
                r.initial[p] = c;
                break;
            }
        }
    }
}

// Each non-Top State carries kTransPerState Transitions on distinct Events,
// targeting other non-Top States (self, sibling, ancestor, or a Composite that
// drills on entry), plus a Top-level catch-all. Distinct Events per Source keep
// every (Source, Event) pair unique. Used by the chain / star / tree shapes.
inline void fillTreeTransitions(ShapeRecipe& r)
{
    for (std::size_t i = 1; i < r.n; ++i)
    {
        for (std::size_t t = 0; t < kTransPerState; ++t)
        {
            std::size_t const event = (i + t) % r.eventCount;
            std::size_t const tgt = 1 + (i + t) % (r.n - 1);  // in [1, n-1], never Top
            r.transitions.push_back({i, event, tgt, /*local=*/false});
        }
    }
    // A Top-level catch-all on the last Event: an Event no Leaf handles defers up
    // the whole parent chain to Top (the handler of last resort).
    r.transitions.push_back({0, r.eventCount - 1, 1, /*local=*/false});
}

}  // namespace detail

// Build the recipe for `shape` at `n` total States (n >= 2). The topology and
// Event width are chosen per shape to isolate that shape's cost axis.
inline ShapeRecipe makeShape(Shape shape, std::size_t n)
{
    if (n < 2)
    {
        n = 2;
    }

    ShapeRecipe r;
    r.n = n;
    r.parent.assign(n, 0);
    r.hasInitial.assign(n, false);
    r.initial.assign(n, 0);

    switch (shape)
    {
        case Shape::DeepChain:
        {
            // A single spine: node i hangs off node i-1, so depth == n-1.
            r.eventCount = detail::kTreeEvents;
            for (std::size_t i = 1; i < n; ++i)
            {
                r.parent[i] = i - 1;
            }
            detail::fillInitials(r);
            detail::fillTreeTransitions(r);
            break;
        }
        case Shape::WideStar:
        {
            // Every non-Top State is a direct child of Top: depth 1, maximal breadth.
            r.eventCount = detail::kTreeEvents;
            for (std::size_t i = 1; i < n; ++i)
            {
                r.parent[i] = 0;
            }
            detail::fillInitials(r);
            detail::fillTreeTransitions(r);
            break;
        }
        case Shape::BalancedTree:
        {
            // A complete binary tree: node i's parent is (i-1)/2, depth ~ log2(n).
            constexpr std::size_t fanout = 2;
            r.eventCount = detail::kTreeEvents;
            for (std::size_t i = 1; i < n; ++i)
            {
                r.parent[i] = (i - 1) / fanout;
            }
            detail::fillInitials(r);
            detail::fillTreeTransitions(r);
            break;
        }
        case Shape::DenseTransitions:
        {
            // A shallow star whose wide Event enum carries a near-complete
            // transition graph: every non-Top State transitions on every Event, so
            // the table holds ~ (n-1) * eventCount Transitions and the validator's
            // O(transitions^2) duplicate-pair scan dominates. eventCount scales with
            // n to keep the graph dense as n grows.
            r.eventCount = n;
            for (std::size_t i = 1; i < n; ++i)
            {
                r.parent[i] = 0;
            }
            detail::fillInitials(r);
            for (std::size_t i = 1; i < n; ++i)
            {
                for (std::size_t e = 0; e < r.eventCount; ++e)
                {
                    std::size_t const tgt = 1 + (i + e) % (n - 1);
                    r.transitions.push_back({i, e, tgt, /*local=*/false});
                }
            }
            break;
        }
        case Shape::WorstCase:
        {
            // A dense comb: odd indices form a deep spine (1 under Top, each later
            // odd node under the previous odd node), even indices hang a Leaf off the
            // spine node just before them. The machine is thus both deep (the spine,
            // ~n/2) and bushy (~n/2 hanging Leaves). Initial Substates drill straight
            // down the spine to the bottom, so the resting configuration is maximally
            // deep; the hanging Leaves are reached via Transitions. Every non-Top
            // State transitions on every Event, so the table is quadratically dense.
            r.eventCount = n;
            for (std::size_t i = 1; i < n; ++i)
            {
                if (i % 2 == 1)
                {
                    r.parent[i] = (i == 1) ? 0 : i - 2;  // spine
                }
                else
                {
                    r.parent[i] = i - 1;  // a Leaf hanging off the spine node before it
                }
            }
            // Top drills into the spine; each spine node drills to the next spine
            // node, or (at the bottom) to its own hanging Leaf.
            r.hasInitial[0] = true;
            r.initial[0] = 1;
            for (std::size_t i = 1; i < n; i += 2)
            {
                if (i + 2 < n)
                {
                    r.hasInitial[i] = true;
                    r.initial[i] = i + 2;  // drill deeper down the spine
                }
                else if (i + 1 < n)
                {
                    r.hasInitial[i] = true;
                    r.initial[i] = i + 1;  // bottom spine node -> its hanging Leaf
                }
            }
            for (std::size_t i = 1; i < n; ++i)
            {
                for (std::size_t e = 0; e < r.eventCount; ++e)
                {
                    std::size_t const tgt = 1 + (i + e) % (n - 1);
                    r.transitions.push_back({i, e, tgt, /*local=*/false});
                }
            }
            break;
        }
    }

    return r;
}

// Emit the recipe as a complete, standalone C++ translation unit on a string. The
// emitted file declares GenState (exactly n enumerators) and GenEvent, spells out
// the builder chain in recipe order, forces a Machine<Table> instantiation (so the
// full reflection codegen and the consteval validator run), and drives the machine
// through runBackbone, exiting nonzero on any divergence / invariant violation /
// vacuous run.
inline std::string shapeToSource(Shape shape, std::size_t n)
{
    ShapeRecipe const r = makeShape(shape, n);

    auto stateRef = [](std::size_t i) { return "GenState::S" + std::to_string(i); };
    auto eventRef = [](std::size_t e) { return "GenEvent::E" + std::to_string(e); };

    std::ostringstream o;
    o << "// Generated by eta_hsm::probe::shapeToSource -- do not edit.\n"
      << "// Shaped machine shape=" << shapeName(shape) << " states=" << r.n << " events=" << r.eventCount << ".\n"
      << "//\n"
      << "// A standalone reproducer: declares its own State/Event enums, builds the\n"
      << "// production Hsm table with explicit builder calls, instantiates a real\n"
      << "// Machine<Table>, and drives it through the exhaustive backbone, exiting\n"
      << "// nonzero on any production-vs-reference divergence or a vacuous run. Compile\n"
      << "// with the cap raised: -DETA_HSM_MAX_STATES (and -DETA_HSM_MAX_TRANSITIONS\n"
      << "// for the dense shape).\n"
      << "\n"
      << "#include <cstddef>\n"
      << "#include <iostream>\n"
      << "\n"
      << "#include \"eta_hsm/machine/hsm.hpp\"\n"
      << "#include \"eta_hsm/machine/machine.hpp\"\n"
      << "#include \"eta_hsm/reference/backbone.hpp\"\n"
      << "\n"
      << "namespace {\n"
      << "using eta_hsm::Hsm;\n"
      << "\n";

    // The State enum: exactly n enumerators, so every enumerator is a declared
    // State and the validator's exhaustiveness check passes with no .unwired calls.
    o << "enum class GenState {\n";
    for (std::size_t i = 0; i < r.n; ++i)
    {
        o << "    S" << i << ",\n";
    }
    o << "};\n";

    // The Event enum, wide enough for the shape's transition graph.
    o << "enum class GenEvent {\n";
    for (std::size_t e = 0; e < r.eventCount; ++e)
    {
        o << "    E" << e << ",\n";
    }
    o << "};\n";

    o << "struct GenHost {};\n"
      << "\n"
      << "// The shaped machine's production table, spelled out call by call.\n"
      << "consteval auto makeShapedTable()\n"
      << "{\n"
      << "    Hsm<GenHost, GenState, GenEvent> m{};\n";

    // Top (the isTop root) and its Initial Substate.
    o << "    m = m.initial(" << stateRef(0) << ", " << stateRef(r.initial[0]) << ");\n";

    // Attach every non-Top State under its parent.
    for (std::size_t i = 1; i < r.n; ++i)
    {
        o << "    m = m.state(" << stateRef(i) << ", " << stateRef(r.parent[i]) << ");\n";
    }

    // Give every non-Top Composite State its Initial Substate.
    for (std::size_t i = 1; i < r.n; ++i)
    {
        if (r.hasInitial[i])
        {
            o << "    m = m.initial(" << stateRef(i) << ", " << stateRef(r.initial[i]) << ");\n";
        }
    }

    // Each Transition: .local for the Local dispatch variant, .on for External.
    for (auto const& t : r.transitions)
    {
        char const* const verb = t.local ? "local" : "on";
        o << "    m = m." << verb << "(" << stateRef(t.src) << ", " << eventRef(t.event) << ", " << stateRef(t.tgt)
          << ");\n";
    }

    o << "    return m;\n"
      << "}\n"
      << "constexpr auto kShapedTable = makeShapedTable();\n"
      << "}  // namespace\n"
      << "\n"
      << "int main()\n"
      << "{\n"
      << "    auto const report = eta_hsm::reference::runBackbone<kShapedTable>();\n"
      << "    for (auto const& f : report.failures)\n"
      << "    {\n"
      << "        std::cerr << \"FAIL: \" << f << \"\\n\";\n"
      << "    }\n"
      << "    if (!report.ok())\n"
      << "    {\n"
      << "        return 1;\n"
      << "    }\n"
      << "    if (report.pairsVerified == 0)\n"
      << "    {\n"
      << "        std::cerr << \"FAIL: vacuous -- no reachable (Leaf, Event) pair verified\\n\";\n"
      << "        return 1;\n"
      << "    }\n"
      << "    std::cout << \"OK shape=" << shapeName(shape) << " N=" << r.n << "\"\n"
      << "              << \" states=\" << kShapedTable.stateCount\n"
      << "              << \" transitions=\" << kShapedTable.transitionCount\n"
      << "              << \" reachableLeaves=\" << report.reachableLeaves\n"
      << "              << \" events=\" << report.eventCount\n"
      << "              << \" pairs=\" << report.pairsVerified\n"
      << "              << \" maxSteps=\" << report.maxExitEntrySteps\n"
      << "              << \" maxDepth=\" << report.maxActivePathDepth\n"
      << "              << \"\\n\";\n"
      << "    return 0;\n"
      << "}\n";

    return o.str();
}

}  // namespace eta_hsm::probe
