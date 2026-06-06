#pragma once

// Mermaid emitter: render a machine's `constexpr` table as a Mermaid
// `stateDiagram-v2` that renders inline in GitHub markdown (so it doubles as the
// README's live diagram of the cd_player machine).
//
// Two Mermaid constraints shape the structure, and are why this diverges from the
// PlantUML emitter (which has neither limit):
//   1. A transition from a Composite State to a State nested inside it is
//      rejected ("Setting X as parent of X would create a cycle"). The Top State
//      has exactly such transitions (e.g. cd_player's Top -> Playing on Hammer),
//      so Top is NOT drawn as a composite block -- it is the diagram frame. Its
//      children render at the top level and its Transitions are listed flat.
//   2. An empty `state X {}` block is rejected ("No such shape: roundedWithTitle").
//      So a Leaf State is emitted as a bare declaration, and only a Composite
//      State (one with children) gets a `state X { ... }` block.
//
// Deeper machines with a transition from an intermediate Composite to its own
// descendant still hit constraint 1 (a Mermaid limitation, not ours); cd_player
// has none, so its diagram renders cleanly.

#include <cstddef>
#include <format>
#include <string>

#include "eta_hsm/diagram/diagram.hpp"

namespace eta_hsm::diagram {

namespace detail {

// Render the subtree rooted at `s` at `indent` (four spaces per level). A
// Composite State becomes a `state X { [*] --> Initial; <children> }` block; a
// Leaf State is a bare declaration line (an empty block is invalid in Mermaid).
template <auto Table, class State>
void render_state_mermaid(std::string& out, State s, std::size_t indent)
{
    std::string const pad(4 * indent, ' ');

    if (!has_children<Table>(s))
    {
        out += std::format("{}{}\n", pad, name_of(s));  // Leaf: bare declaration.
        return;
    }

    std::string const inner(4 * (indent + 1), ' ');
    out += std::format("{}state {} {{\n", pad, name_of(s));
    if (auto const* row = state_row<Table>(s); row != nullptr && row->hasInitial)
    {
        out += std::format("{}[*] --> {}\n", inner, name_of(row->initial));
    }
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].parent == s && Table.states[i].state != s)
        {
            render_state_mermaid<Table>(out, Table.states[i].state, indent + 1);
        }
    }
    out += std::format("{}}}\n", pad);
}

}  // namespace detail

// A Mermaid `stateDiagram-v2` for `Table`.
template <auto Table>
inline std::string to_mermaid()
{
    using State = typename decltype(Table)::State;
    constexpr auto actions = action_labels<Table>();
    constexpr auto guards = guard_labels<Table>();

    State const top = top_state<Table>();

    std::string out = "stateDiagram-v2\n";
    // Top is the diagram frame, not a drawn composite (see the file header). Emit
    // its Initial-Substate arrow, then its children, then every Transition flat.
    if (auto const* row = state_row<Table>(top); row != nullptr && row->hasInitial)
    {
        out += std::format("    [*] --> {}\n", name_of(row->initial));
    }
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].parent == top && Table.states[i].state != top)
        {
            detail::render_state_mermaid<Table>(out, Table.states[i].state, 1);
        }
    }
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        auto const& tr = Table.transitions[i];
        std::string const label = transition_label(tr, actions[i], guards[i]);
        if (tr.internal)
        {
            out += std::format("    {} : {}\n", name_of(tr.source), label);
        }
        else
        {
            out += std::format("    {} --> {} : {}\n", name_of(tr.source), name_of(tr.target), label);
        }
    }
    return out;
}

}  // namespace eta_hsm::diagram
