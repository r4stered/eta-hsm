#pragma once

// Mermaid emitter: render a machine's `constexpr` table as a Mermaid
// `stateDiagram-v2`. Structure mirrors the PlantUML emitter -- nested
// `state X { ... }` blocks with `[*] --> Initial` arrows for Composite States,
// then a flat list of Transitions as `Source --> Target : Event [Guard] / Action`
// (an Internal Transition as a `Source : Event / Action` description line). The
// Mermaid form renders inline in GitHub markdown, so it doubles as the README's
// live diagram of the cd_player machine.

#include <cstddef>
#include <string>

#include "eta_hsm/diagram/diagram.hpp"

namespace eta_hsm::diagram {

namespace detail {

// Render the subtree rooted at `s` as nested `state {}` blocks at `indent`
// (four spaces per level, Mermaid's idiom). A Composite State emits its
// `[*] --> Initial` arrow, then its children in declaration order.
template <auto Table, class State>
void render_state_mermaid(std::string& out, State s, std::size_t indent)
{
    std::string const pad(4 * indent, ' ');
    std::string const inner(4 * (indent + 1), ' ');

    out += pad + "state " + name_of(s) + " {\n";
    if (auto const* row = state_row<Table>(s); row != nullptr && row->hasInitial)
    {
        out += inner + "[*] --> " + name_of(row->initial) + "\n";
    }
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].parent == s && Table.states[i].state != s)
        {
            render_state_mermaid<Table>(out, Table.states[i].state, indent + 1);
        }
    }
    out += pad + "}\n";
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
    out += "    state " + name_of(top) + " {\n";
    if (auto const* row = state_row<Table>(top); row != nullptr && row->hasInitial)
    {
        out += "        [*] --> " + name_of(row->initial) + "\n";
    }
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].parent == top && Table.states[i].state != top)
        {
            detail::render_state_mermaid<Table>(out, Table.states[i].state, 2);
        }
    }
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        auto const& tr = Table.transitions[i];
        std::string const label = transition_label(tr, actions[i], guards[i]);
        if (tr.internal)
        {
            out += "        " + name_of(tr.source) + " : " + label + "\n";
        }
        else
        {
            out += "        " + name_of(tr.source) + " --> " + name_of(tr.target) + " : " + label + "\n";
        }
    }
    out += "    }\n";
    return out;
}

}  // namespace eta_hsm::diagram
