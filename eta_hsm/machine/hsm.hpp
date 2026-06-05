#pragma once

// eta_hsm v2 machine description: a state machine is a single `constexpr` table
// value produced by a fluent builder (ADR-0002). The table is the single source
// of truth that the generated Dispatch (machine.hpp) reads at compile time.
//
// This slice (issue 0002) supports FLAT machines only: a single Top State with
// Leaf children one level deep, transitions of the form (Source, Event, Target)
// with an optional Action, and one level of parent deferral (Leaf -> Top). No
// Guards, no deeper hierarchy, no Local semantics yet.

#include <array>
#include <cstddef>

namespace eta_hsm {

// Fixed capacities for the builder's constexpr storage. Generous for the spike;
// the final table only ever exposes the populated prefix via its counts.
inline constexpr std::size_t kMaxStates = 64;
inline constexpr std::size_t kMaxTransitions = 256;

// One row of the State tree: a State, its parent, and (if Composite) the Initial
// Substate the machine forwards into when the State is entered.
template <class StateEnum>
struct StateRow {
    StateEnum state{};
    StateEnum parent{};
    bool isTop{false};       // the single root; has no parent
    bool hasInitial{false};  // true once an Initial Substate is declared
    StateEnum initial{};     // the Initial Substate, when hasInitial
};

// One Transition: on `event` while in (or deferring through) `source`, move to
// `target`, running `action` on the Host between Exit and Entry if non-null.
template <class StateEnum, class EventEnum, class Host>
struct TransitionRow {
    StateEnum source{};
    EventEnum event{};
    StateEnum target{};
    void (Host::*action)() = nullptr;
};

// The fluent builder and the table value are the same type: every modifier
// returns a new value with one more row, so the final chained expression is a
// structural `constexpr` value usable as a non-type template argument to bind
// the generated Dispatch (see Machine in machine.hpp).
template <class Host, class StateEnum, class EventEnum>
struct Hsm {
    using HostType = Host;
    using State = StateEnum;
    using Event = EventEnum;

    std::array<StateRow<StateEnum>, kMaxStates> states{};
    std::size_t stateCount{0};
    std::array<TransitionRow<StateEnum, EventEnum, Host>, kMaxTransitions> transitions{};
    std::size_t transitionCount{0};

    // Declare a State and its parent.
    constexpr Hsm state(StateEnum s, StateEnum parent) const
    {
        Hsm next = *this;
        next.states[next.stateCount++] = StateRow<StateEnum>{s, parent, false, false, {}};
        return next;
    }

    // Declare `parent`'s Initial Substate. Registers `parent` as a State if it
    // was not declared via .state (the Top State is declared this way).
    constexpr Hsm initial(StateEnum parent, StateEnum child) const
    {
        Hsm next = *this;
        StateRow<StateEnum>* row = next.find(parent);
        if (row == nullptr) {
            next.states[next.stateCount] = StateRow<StateEnum>{parent, parent, true, false, {}};
            row = &next.states[next.stateCount];
            ++next.stateCount;
        }
        row->hasInitial = true;
        row->initial = child;
        return next;
    }

    // Declare a Transition (Source, Event, Target) with no Action.
    constexpr Hsm on(StateEnum source, EventEnum event, StateEnum target) const
    {
        Hsm next = *this;
        next.transitions[next.transitionCount++] =
            TransitionRow<StateEnum, EventEnum, Host>{source, event, target, nullptr};
        return next;
    }

private:
    constexpr StateRow<StateEnum>* find(StateEnum s)
    {
        for (std::size_t i = 0; i < stateCount; ++i) {
            if (states[i].state == s) {
                return &states[i];
            }
        }
        return nullptr;
    }
};

}  // namespace eta_hsm
