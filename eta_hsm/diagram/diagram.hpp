#pragma once

// Shared machinery for the diagram emitters (plantuml.hpp / mermaid.hpp). A
// diagram is produced by walking a machine's `constexpr` table -- the single
// source of truth -- so it can never drift from the machine it documents. State
// nesting comes from the table's tree topology; Transition labels carry the exact
// Event, Guard, and Action names.
//
// State and Event names are recovered at runtime from enum reflection
// (enum_name). Action and Guard names are the hard part: the table stores them as
// opaque pointer-to-member values, and the identifier lives only in the Host's
// declaration. resolve_member walks the Host's members via P2996, forms a
// pointer-to-member for each, and returns the identifier of the one whose pointer
// equals the stored Action/Guard -- the same splice-by-reflection technique the
// machine's hook detection uses, run here at compile time.

#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::diagram {

namespace detail {

// Resolve a Host member-function pointer (an Action or a Guard) to its declared
// identifier. Returns an empty view for a null pointer (no Action / no Guard).
// `Ptr` is the exact member-pointer type, so the `requires` guard skips every
// member whose signature does not match, and pointer equality then picks out the
// single member the table row refers to.
template <class Host, class Ptr>
consteval std::string_view resolve_member(Ptr p)
{
    if (p == nullptr)
    {
        return {};
    }
    std::string_view name{};
    template for (constexpr std::meta::info m :
                  std::define_static_array(std::meta::members_of(^^Host, std::meta::access_context::current())))
    {
        if constexpr (std::meta::is_function(m) && !std::meta::is_special_member_function(m))
        {
            // Only members whose pointer-to-member type is exactly `Ptr` can match;
            // the rest (wrong signature, static, parameterized hooks) fail the cast
            // and are skipped. Among the survivors, pointer equality is the
            // discriminator -- two same-signature members are told apart by value.
            if constexpr (requires { static_cast<Ptr>(&[:m:]); })
            {
                if (static_cast<Ptr>(&[:m:]) == p)
                {
                    name = std::meta::identifier_of(m);
                }
            }
        }
    }
    return name;
}

}  // namespace detail

// Identifier of the Action member `p` names, or empty if `p` is null.
template <class Host>
consteval std::string_view action_name(void (Host::*p)())
{
    return detail::resolve_member<Host>(p);
}

// Identifier of the Guard member `p` names, or empty if `p` is null.
template <class Host>
consteval std::string_view guard_name(bool (Host::*p)() const)
{
    return detail::resolve_member<Host>(p);
}

// The resolved Action names for every Transition of `Table`, in row order. Built
// once at compile time so the runtime emitter can index it by Transition.
template <auto Table>
consteval std::array<std::string_view, Table.transitionCount> action_labels()
{
    std::array<std::string_view, Table.transitionCount> out{};
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        out[i] = action_name(Table.transitions[i].action);
    }
    return out;
}

// The resolved Guard names for every Transition of `Table`, in row order.
template <auto Table>
consteval std::array<std::string_view, Table.transitionCount> guard_labels()
{
    std::array<std::string_view, Table.transitionCount> out{};
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        out[i] = guard_name(Table.transitions[i].guard);
    }
    return out;
}

// The single Top State of `Table`.
template <auto Table>
constexpr typename decltype(Table)::State top_state()
{
    using State = typename decltype(Table)::State;
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].isTop)
        {
            return Table.states[i].state;
        }
    }
    return State{};
}

// The State-tree row for `s`, or nullptr if `s` is not a declared State.
template <auto Table, class State>
constexpr const StateRow<State>* state_row(State s)
{
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].state == s)
        {
            return &Table.states[i];
        }
    }
    return nullptr;
}

// Human-readable name of a State or Event enumerator, or "?" if it is not a
// declared enumerator (which a well-formed table never contains).
template <class Enum>
inline std::string name_of(Enum value)
{
    auto const name = enum_name(value);
    return name ? std::string{*name} : std::string{"?"};
}

// The label for a Transition: "Event[ [Guard]][ / Action][ (local)]". `action`
// and `guard` are the resolved identifier views (empty when absent). The "(local)"
// marker distinguishes a Local Transition from an External one: the two otherwise
// share Source/Target/Event but differ in whether the shared ancestor is exited
// and re-entered, so the diagram must not render them identically. Shared by both
// format emitters; only the arrow/description framing around it differs.
template <class TransitionRowT>
inline std::string transition_label(TransitionRowT const& tr, std::string_view action, std::string_view guard)
{
    std::string label = name_of(tr.event);
    if (!guard.empty())
    {
        label += " [" + std::string{guard} + "]";
    }
    if (!action.empty())
    {
        label += " / " + std::string{action};
    }
    if (tr.local)
    {
        label += " (local)";
    }
    return label;
}

}  // namespace eta_hsm::diagram
