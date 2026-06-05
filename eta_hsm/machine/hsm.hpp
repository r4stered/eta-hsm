#pragma once

// eta_hsm v2 machine description: a state machine is a single `constexpr` table
// value produced by a fluent builder (ADR-0002). The table is the single source
// of truth that the generated Dispatch (machine.hpp) reads at compile time.
//
// This slice supports full HIERARCHY (issue 0004): composite States nested to
// any depth, cross-level Transitions that run the ordered Exit/Entry chain
// through the least-common-ancestor, recursive Initial-Substate forwarding, and
// parent deferral up the whole tree. Transitions are (Source, Event, Target)
// with an optional Action and an optional Guard (issue 0003); .internal declares
// an Internal Transition (Action only, no State change); .local declares a Local
// Transition (it does not Exit/re-enter the shared ancestor in parent/child
// cases). .on declares an External Transition, the default.

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
// `target`, running `action` on the Host between Exit and Entry if non-null. A
// non-null `guard` is consulted first: the Transition is taken only when the
// Guard returns true, otherwise the Event keeps deferring up the parent chain.
template <class StateEnum, class EventEnum, class Host>
struct TransitionRow {
    StateEnum source{};
    EventEnum event{};
    StateEnum target{};
    void (Host::*action)() = nullptr;
    bool (Host::*guard)() const = nullptr;
    bool internal{false};  // Internal Transition: run `action` only, no State change, no Exit/Entry
    bool local{false};     // Local Transition: skip Exit/re-entry of the shared ancestor (parent/child cases)
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

    // Declare a Transition (Source, Event, Target), optionally running `action`
    // on the Host between Exit and Entry and optionally conditioned by `guard`.
    // The Transition is taken only when `guard` is null or returns true.
    constexpr Hsm on(StateEnum source, EventEnum event, StateEnum target,
                     void (Host::*action)() = nullptr,
                     bool (Host::*guard)() const = nullptr) const
    {
        Hsm next = *this;
        next.transitions[next.transitionCount++] =
            TransitionRow<StateEnum, EventEnum, Host>{source, event, target, action, guard, false, false};
        return next;
    }

    // Declare a Local Transition (Source, Event, Target). Identical to .on except
    // that when Source and Target are in a parent/child relationship the shared
    // ancestor is not Exited and re-entered. For unrelated States it behaves like
    // an External Transition. External (.on) is the default.
    constexpr Hsm local(StateEnum source, EventEnum event, StateEnum target,
                        void (Host::*action)() = nullptr,
                        bool (Host::*guard)() const = nullptr) const
    {
        Hsm next = *this;
        next.transitions[next.transitionCount++] =
            TransitionRow<StateEnum, EventEnum, Host>{source, event, target, action, guard, false, true};
        return next;
    }

    // Declare an Internal Transition: on `event` while in `source`, run `action`
    // on the Host -- no State change, no Exit/Entry. Optionally conditioned by
    // `guard`. The target slot is unused, so it is set to `source`.
    constexpr Hsm internal(StateEnum source, EventEnum event, void (Host::*action)(),
                           bool (Host::*guard)() const = nullptr) const
    {
        Hsm next = *this;
        next.transitions[next.transitionCount++] =
            TransitionRow<StateEnum, EventEnum, Host>{source, event, source, action, guard, true, false};
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
