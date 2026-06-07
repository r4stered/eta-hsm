#pragma once

// A generator of random *valid* machine tables, as runtime data, for fuzzing the
// reference interpreter (reference_interpreter.hpp) on machine shapes nobody would
// hand-write. A seed deterministically produces a RandomRecipe -- a plain
// description of a machine's States, parent links, Initial Substates, and
// Transitions -- which materializes into a runtime TableView the interpreter
// walks directly.
//
// The recipe is materialization-agnostic: it is expressed as indices into the
// fixed ProbeState / ProbeEvent enums (scaling_generator.hpp), so besides the
// runtime TableView built here it can also be emitted as C++ source for a compiled
// `Machine<Table>` -- one description of a machine, not one per consumer.
//
// Ceiling: walking a runtime TableView never instantiates the production NTTP
// dispatch (`template for`). This generator hardens the semantic model and the
// notion of a well-formed table; it does not exercise the real dispatcher.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "eta_hsm/probe/scaling_generator.hpp"
#include "eta_hsm/reference/reference_interpreter.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::reference {

using probe::ProbeEvent;
using probe::ProbeHost;
using probe::ProbeState;

// The most Transitions any one generated State carries (also bounded by the Event
// count, so every Transition on a State lands on a distinct Event).
inline constexpr std::size_t kMaxTransPerState = 4;

// One State of a recipe, by enumerator index. `parent` is meaningful unless
// `isTop`; `initial` is meaningful unless not `hasInitial`.
struct RecipeState {
    std::size_t index{0};
    std::size_t parent{0};
    bool isTop{false};
    bool hasInitial{false};
    std::size_t initial{0};
};

// One Transition of a recipe, by enumerator index. Guard-free (the probe Host has
// no Guards); `internal`/`local` select the dispatch variant.
struct RecipeTransition {
    std::size_t source{0};
    std::size_t event{0};
    std::size_t target{0};
    bool internal{false};
    bool local{false};
};

// A complete description of one generated machine. Plain data: the seam a future
// codegen fuzzer reuses to emit C++ source instead of a runtime view.
struct RandomRecipe {
    std::vector<RecipeState> states;
    std::vector<RecipeTransition> transitions;
};

// A deterministic PRNG (splitmix64). Seed-reproducible and independent of
// <random>'s engine, so a given seed names exactly one machine.
struct Prng {
    std::uint64_t state{0};

    constexpr std::uint64_t next()
    {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // A uniform value in [0, n); 0 when n == 0.
    constexpr std::size_t below(std::size_t n) { return n == 0 ? 0 : next() % n; }
};

// Generate a random valid machine of `stateCount` States from `seed`. Node 0 is
// Top; each later node attaches to a uniformly random earlier node, so the result
// is always a single-rooted tree (no cycles, no forest). Every Composite node
// takes its first child as Initial Substate, so the machine drills to a Leaf.
// `stateCount` is clamped to [2, capacity of ProbeState].
//
// constexpr so the same recipe can be re-derived inside constant evaluation, where
// it materializes into a compile-time production `Hsm` table (random_codegen.hpp).
constexpr RandomRecipe makeRecipe(std::uint64_t seed, std::size_t stateCount)
{
    constexpr std::size_t cap = eta_hsm::enum_values<ProbeState>().size();
    if (stateCount < 2)
    {
        stateCount = 2;
    }
    if (stateCount > cap)
    {
        stateCount = cap;
    }

    Prng rng{seed};
    RandomRecipe r;

    r.states.push_back({0, 0, /*isTop=*/true, false, 0});
    for (std::size_t i = 1; i < stateCount; ++i)
    {
        std::size_t const parent = rng.below(i);  // a uniformly random earlier node
        r.states.push_back({i, parent, false, false, 0});
    }

    // Each node that is some node's parent is Composite; give it its first child
    // as Initial Substate so the interpreter can drill it to a Leaf.
    for (std::size_t p = 0; p < stateCount; ++p)
    {
        for (std::size_t c = 1; c < stateCount; ++c)
        {
            if (r.states[c].parent == p)
            {
                r.states[p].hasInitial = true;
                r.states[p].initial = c;
                break;
            }
        }
    }

    // Each non-Top State carries 1..kMaxTransPerState Transitions on distinct
    // random Events, targeting random non-Top States (self, sibling, ancestor, or
    // a Composite that drills on entry). Distinct Events per Source keep every
    // (Source, Event) pair unique, so the table stays well-formed.
    constexpr std::size_t numEvents = eta_hsm::enum_values<ProbeEvent>().size();
    std::size_t const cap2 = std::min<std::size_t>(kMaxTransPerState, numEvents);
    for (std::size_t i = 1; i < stateCount; ++i)
    {
        std::size_t const k = 1 + rng.below(cap2);  // at least one Transition
        std::size_t const firstEvent = rng.below(numEvents);
        for (std::size_t t = 0; t < k; ++t)
        {
            std::size_t const event = (firstEvent + t) % numEvents;  // distinct per Source
            std::size_t const target = 1 + rng.below(stateCount - 1);  // a non-Top State
            bool const local = rng.below(2) == 0;
            r.transitions.push_back({i, event, target, /*internal=*/false, local});
        }
    }

    // A Top-level catch-all on one Event, so an Event no Leaf handles defers up the
    // whole parent chain to Top -- exercising the handler of last resort. The other
    // Events are left potentially unhandled, so the strict-no-op path is reached too.
    r.transitions.push_back({0, rng.below(numEvents), 1 + rng.below(stateCount - 1), false, false});

    return r;
}

// The verdict of a runtime well-formedness check over a TableView. `ok` is the
// table is a valid machine; `error` names the first fault otherwise.
struct WellFormed {
    bool ok{true};
    std::string error;
};

// The enumerator identifier of `s`, or a placeholder if undeclared.
template <class S>
inline std::string stateName(S s)
{
    auto const n = eta_hsm::enum_name(s);
    return std::string{n ? *n : "<?>"};
}

// True if walking `s` up the parent chain reaches Top within the State count --
// the State is anchored to a single root with no cycle. The step cap stands in
// for a visited-set: a parent cycle never reaches Top before the count runs out.
template <class S, class E, class H>
bool anchoredToTop(const TableView<S, E, H>& view, S s)
{
    S cur = s;
    for (std::size_t i = 0; i <= view.states.size(); ++i)
    {
        const auto* row = view.find(cur);
        if (row == nullptr)
        {
            return false;
        }
        if (row->isTop)
        {
            return true;
        }
        cur = row->parent;
    }
    return false;
}

// True if some declared State has `s` as its parent -- i.e. `s` is Composite and
// must therefore carry an Initial Substate to drill to a Leaf.
template <class S, class E, class H>
bool isComposite(const TableView<S, E, H>& view, S s)
{
    for (auto const& row : view.states)
    {
        if (!row.isTop && row.parent == s)
        {
            return true;
        }
    }
    return false;
}

// Check a runtime table is a well-formed machine -- the runtime counterpart of the
// compile-time validator's structural checks, expressed over a TableView so a
// generated table can be validated without the NTTP path. The first fault wins.
template <class S, class E, class H>
WellFormed wellFormed(const TableView<S, E, H>& view)
{
    std::size_t tops = 0;
    for (auto const& r : view.states)
    {
        if (r.isTop)
        {
            ++tops;
        }
    }
    if (tops == 0)
    {
        return {false, "no Top State"};
    }
    if (tops > 1)
    {
        return {false, "multiple Top States"};
    }

    for (auto const& r : view.states)
    {
        if (!r.isTop && view.find(r.parent) == nullptr)
        {
            return {false, "parent of " + stateName(r.state) + " is not a declared State"};
        }
        if (isComposite(view, r.state) && !r.hasInitial)
        {
            return {false, "Composite State " + stateName(r.state) + " has no Initial Substate"};
        }
        if (r.hasInitial)
        {
            const auto* init = view.find(r.initial);
            if (init == nullptr || init->isTop || init->parent != r.state)
            {
                return {false, "Initial Substate of " + stateName(r.state) + " is not a child"};
            }
        }
        if (!anchoredToTop(view, r.state))
        {
            return {false, stateName(r.state) + " is not anchored to Top"};
        }
    }

    for (auto const& t : view.transitions)
    {
        if (view.find(t.source) == nullptr)
        {
            return {false, "Transition source " + stateName(t.source) + " is not declared"};
        }
        if (view.find(t.target) == nullptr)
        {
            return {false, "Transition target " + stateName(t.target) + " is not declared"};
        }
    }

    // No two Transitions share a (Source, Event) without distinct Guards. A pair on
    // the same Source and Event is ambiguous unless both carry a non-null Guard and
    // the two Guard pointers differ -- the only case where dispatch can pick one.
    for (std::size_t i = 0; i < view.transitions.size(); ++i)
    {
        for (std::size_t j = i + 1; j < view.transitions.size(); ++j)
        {
            auto const& a = view.transitions[i];
            auto const& b = view.transitions[j];
            bool const distinctGuards = a.guard != nullptr && b.guard != nullptr && a.guard != b.guard;
            if (a.source == b.source && a.event == b.event && !distinctGuards)
            {
                return {false, "ambiguous (Source, Event) on " + stateName(a.source)};
            }
        }
    }

    return {true, {}};
}

// Materialize a recipe into the runtime TableView the interpreter walks.
inline TableView<ProbeState, ProbeEvent, ProbeHost> recipeToView(const RandomRecipe& r)
{
    constexpr auto states = eta_hsm::enum_values<ProbeState>();
    constexpr auto events = eta_hsm::enum_values<ProbeEvent>();

    TableView<ProbeState, ProbeEvent, ProbeHost> view;
    for (auto const& s : r.states)
    {
        view.states.push_back({states[s.index], states[s.parent], s.isTop, s.hasInitial, states[s.initial]});
    }
    for (auto const& t : r.transitions)
    {
        view.transitions.push_back(
            {states[t.source], events[t.event], states[t.target], nullptr, nullptr, t.internal, t.local});
    }
    return view;
}

}  // namespace eta_hsm::reference
