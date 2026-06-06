#pragma once

// PlantUML emitter: render a machine's `constexpr` table as a PlantUML state
// diagram. The State tree becomes nested `state X { ... }` blocks (with a
// `[*] --> Initial` arrow for each Composite State); every Transition is then
// listed inside the Top block as `Source --> Target : Event [Guard] / Action`
// (an Internal Transition as `Source : Event / Action`, no arrow). Names come
// straight from the table via reflection, so the diagram tracks the machine.

#include <cstddef>
#include <format>
#include <string>

#include "eta_hsm/diagram/diagram.hpp"

namespace eta_hsm::diagram {

namespace detail {

// Render the subtree rooted at `s` as nested `state {}` blocks at `indent`
// (two spaces per level). A Composite State emits its `[*] --> Initial` arrow,
// then its children in declaration order. Transitions are not emitted here --
// they are listed once, flat, after the whole tree.
template <auto Table, class State>
void render_state_plantuml(std::string& out, State s, std::size_t indent)
{
    std::string const pad(2 * indent, ' ');
    std::string const inner(2 * (indent + 1), ' ');

    out += std::format("{}state {} {{\n", pad, name_of(s));
    if (auto const* row = state_row<Table>(s); row != nullptr && row->hasInitial)
    {
        out += std::format("{}[*] --> {}\n", inner, name_of(row->initial));
    }
    // Children in declaration order: rows whose parent is `s` (excluding `s`
    // itself, since the Top State is its own parent).
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].parent == s && Table.states[i].state != s)
        {
            render_state_plantuml<Table>(out, Table.states[i].state, indent + 1);
        }
    }
    out += std::format("{}}}\n", pad);
}

}  // namespace detail

// A PlantUML state diagram for `Table`, as a `@startuml ... @enduml` document.
template <auto Table>
inline std::string to_plantuml()
{
    using State = typename decltype(Table)::State;
    constexpr auto actions = action_labels<Table>();
    constexpr auto guards = guard_labels<Table>();

    State const top = top_state<Table>();

    std::string out = "@startuml\n";
    out += std::format("state {} {{\n", name_of(top));
    if (auto const* row = state_row<Table>(top); row != nullptr && row->hasInitial)
    {
        out += std::format("  [*] --> {}\n", name_of(row->initial));
    }
    // The State tree first: Top's children, each as a nested block.
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].parent == top && Table.states[i].state != top)
        {
            detail::render_state_plantuml<Table>(out, Table.states[i].state, 1);
        }
    }
    // Then every Transition, flat, inside the Top block so all endpoints are in
    // scope.
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        auto const& tr = Table.transitions[i];
        std::string const label = transition_label(tr, actions[i], guards[i]);
        if (tr.internal)
        {
            out += std::format("  {} : {}\n", name_of(tr.source), label);
        }
        else
        {
            out += std::format("  {} --> {} : {}\n", name_of(tr.source), name_of(tr.target), label);
        }
    }
    out += "}\n";
    out += "@enduml\n";
    return out;
}

}  // namespace eta_hsm::diagram
