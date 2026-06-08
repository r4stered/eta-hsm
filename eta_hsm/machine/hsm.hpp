#pragma once

// eta_hsm machine description: a state machine is a single `constexpr` table
// value produced by a fluent builder (ADR-0002). The table is the single source
// of truth that the generated Dispatch (machine.hpp) reads at compile time.
//
// The description supports full HIERARCHY: composite States nested to
// any depth, cross-level Transitions that run the ordered Exit/Entry chain
// through the least-common-ancestor, recursive Initial-Substate forwarding, and
// parent deferral up the whole tree. Transitions are (Source, Event, Target)
// with an optional Action and an optional Guard; .internal declares
// an Internal Transition (Action only, no State change); .local declares a Local
// Transition (it does not Exit/re-enter the shared ancestor in parent/child
// cases). .on declares an External Transition, the default.

#include <array>
#include <cstddef>

namespace eta_hsm {

// Fixed capacities for the builder's constexpr storage. Generously sized;
// the final table only ever exposes the populated prefix via its counts.
//
// Each capacity defaults to a value that fits every machine in the tree, but is
// overridable per translation unit with a `-D` on the command line
// (`-DETA_HSM_MAX_STATES=…` / `-DETA_HSM_MAX_TRANSITIONS=…`) so a consumer with a
// larger machine can raise the ceiling without editing this header. With no
// override the defaults stand and the table storage is byte-identical to the
// fixed-capacity form.
#ifndef ETA_HSM_MAX_STATES
#define ETA_HSM_MAX_STATES 64
#endif
#ifndef ETA_HSM_MAX_TRANSITIONS
#define ETA_HSM_MAX_TRANSITIONS 256
#endif

inline constexpr std::size_t kMaxStates = ETA_HSM_MAX_STATES;
inline constexpr std::size_t kMaxTransitions = ETA_HSM_MAX_TRANSITIONS;

// One row of the State tree: a State, its parent, and (if Composite) the Initial
// Substate the machine forwards into when the State is entered.
template <class StateEnum>
struct StateRow {
    StateEnum state{};
    StateEnum parent{};
    bool isTop{false};  // the single root; has no parent
    bool hasInitial{false};  // true once an Initial Substate is declared
    StateEnum initial{};  // the Initial Substate, when hasInitial
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
    bool local{false};  // Local Transition: skip Exit/re-entry of the shared ancestor (parent/child cases)
};

// Sentinel for an enum slot the builder has not yet deduced. `Hsm<Host>` enters
// the builder with both the State and Event enums unknown; each is filled in
// (replacing Deduce) the first time a call mentions it -- State from the first
// .state/.initial/.unwired, Event from the first .on/.local/.internal. The fully
// deduced result is the same type and value as the explicit Hsm<Host, State,
// Event>{} build, so this is purely front-end sugar over the table below.
struct Deduce {};

// `Hsm<Host>` and `Hsm<Host, State>` are the partially-deduced builder stages;
// `Hsm<Host, State, Event>` (both enums known) is the final table. The defaults
// route the sugar entry points to the stage specializations further down.
template <class Host, class StateEnum = Deduce, class EventEnum = Deduce>
struct Hsm;

// The fluent builder and the table value are the same type: every modifier
// returns a new value with one more row, so the final chained expression is a
// structural `constexpr` value usable as a non-type template argument to bind
// the generated Dispatch (see Machine in machine.hpp). This fully-deduced stage
// (both enums known) is what every downstream consumer sees.
template <class Host, class StateEnum, class EventEnum>
struct Hsm {
    using HostType = Host;
    using State = StateEnum;
    using Event = EventEnum;

    std::array<StateRow<StateEnum>, kMaxStates> states{};
    std::size_t stateCount{0};
    std::array<TransitionRow<StateEnum, EventEnum, Host>, kMaxTransitions> transitions{};
    std::size_t transitionCount{0};
    // Enumerators deliberately not yet wired into the tree. The exhaustiveness
    // check skips these, so a work-in-progress machine with planned-but-unbuilt
    // States still validates. Everything else must be a declared State.
    std::array<StateEnum, kMaxStates> unwiredStates{};
    std::size_t unwiredCount{0};

    // Declare a State and its parent.
    constexpr Hsm state(StateEnum s, StateEnum parent) const
    {
        Hsm next = *this;
        // Guard the array write but always record the count, so an overflow past
        // kMaxStates is representable (stateCount > capacity) without an OOB write;
        // the validator turns that into a named "too many States" diagnostic.
        if (next.stateCount < kMaxStates)
        {
            next.states[next.stateCount] = StateRow<StateEnum>{s, parent, false, false, {}};
        }
        ++next.stateCount;
        return next;
    }

    // Declare `parent`'s Initial Substate. Registers `parent` as a State if it
    // was not declared via .state (the Top State is declared this way).
    constexpr Hsm initial(StateEnum parent, StateEnum child) const
    {
        Hsm next = *this;
        StateRow<StateEnum>* row = next.find(parent);
        if (row == nullptr)
        {
            // Same overflow handling as state(): guard the write, always count.
            // When already at capacity we cannot store the new row, so leave the
            // Initial unset -- the table is already over-large and the validator's
            // capacity check fires first regardless.
            if (next.stateCount < kMaxStates)
            {
                next.states[next.stateCount] = StateRow<StateEnum>{parent, parent, true, false, {}};
                row = &next.states[next.stateCount];
            }
            ++next.stateCount;
        }
        if (row != nullptr)
        {
            row->hasInitial = true;
            row->initial = child;
        }
        return next;
    }

    // Declare a Transition (Source, Event, Target), optionally running `action`
    // on the Host between Exit and Entry and optionally conditioned by `guard`.
    // The Transition is taken only when `guard` is null or returns true.
    constexpr Hsm on(StateEnum source, EventEnum event, StateEnum target, void (Host::*action)() = nullptr,
                     bool (Host::*guard)() const = nullptr) const
    {
        Hsm next = *this;
        if (next.transitionCount < kMaxTransitions)
        {
            next.transitions[next.transitionCount] =
                TransitionRow<StateEnum, EventEnum, Host>{source, event, target, action, guard, false, false};
        }
        ++next.transitionCount;
        return next;
    }

    // Declare a Local Transition (Source, Event, Target). Identical to .on except
    // that when Source and Target are in a parent/child relationship the shared
    // ancestor is not Exited and re-entered. For unrelated States it behaves like
    // an External Transition. External (.on) is the default.
    constexpr Hsm local(StateEnum source, EventEnum event, StateEnum target, void (Host::*action)() = nullptr,
                        bool (Host::*guard)() const = nullptr) const
    {
        Hsm next = *this;
        if (next.transitionCount < kMaxTransitions)
        {
            next.transitions[next.transitionCount] =
                TransitionRow<StateEnum, EventEnum, Host>{source, event, target, action, guard, false, true};
        }
        ++next.transitionCount;
        return next;
    }

    // Declare an Internal Transition: on `event` while in `source`, run `action`
    // on the Host -- no State change, no Exit/Entry. Optionally conditioned by
    // `guard`. The target slot is unused, so it is set to `source`.
    constexpr Hsm internal(StateEnum source, EventEnum event, void (Host::*action)(),
                           bool (Host::*guard)() const = nullptr) const
    {
        Hsm next = *this;
        if (next.transitionCount < kMaxTransitions)
        {
            next.transitions[next.transitionCount] =
                TransitionRow<StateEnum, EventEnum, Host>{source, event, source, action, guard, true, false};
        }
        ++next.transitionCount;
        return next;
    }

    // Mark `s` as deliberately not-yet-wired, opting it out of the exhaustiveness
    // check. Use while a machine is under construction so a planned
    // enumerator with no State row yet does not fail validation.
    constexpr Hsm unwired(StateEnum s) const
    {
        Hsm next = *this;
        if (next.unwiredCount < kMaxStates)
        {
            next.unwiredStates[next.unwiredCount] = s;
        }
        ++next.unwiredCount;
        return next;
    }

private:
    constexpr StateRow<StateEnum>* find(StateEnum s)
    {
        for (std::size_t i = 0; i < stateCount; ++i)
        {
            if (states[i].state == s)
            {
                return &states[i];
            }
        }
        return nullptr;
    }
};

// Stage 1: the State enum is deduced (from the first .state/.initial/.unwired),
// the Event enum is still pending. Holds the State tree built so far; the first
// .on/.local/.internal deduces the Event enum and promotes the accumulated rows
// into the final Hsm<Host, StateEnum, EventEnum> table above, which is where the
// Transition is recorded. The State-side modifiers mirror the final stage exactly
// -- they take the concrete StateEnum, so mixing a second State enum is a plain
// no-viable-conversion compile error.
template <class Host, class StateEnum>
struct Hsm<Host, StateEnum, Deduce> {
    std::array<StateRow<StateEnum>, kMaxStates> states{};
    std::size_t stateCount{0};
    std::array<StateEnum, kMaxStates> unwiredStates{};
    std::size_t unwiredCount{0};

    constexpr Hsm state(StateEnum s, StateEnum parent) const
    {
        Hsm next = *this;
        if (next.stateCount < kMaxStates)
        {
            next.states[next.stateCount] = StateRow<StateEnum>{s, parent, false, false, {}};
        }
        ++next.stateCount;
        return next;
    }

    constexpr Hsm initial(StateEnum parent, StateEnum child) const
    {
        Hsm next = *this;
        StateRow<StateEnum>* row = next.find(parent);
        if (row == nullptr)
        {
            if (next.stateCount < kMaxStates)
            {
                next.states[next.stateCount] = StateRow<StateEnum>{parent, parent, true, false, {}};
                row = &next.states[next.stateCount];
            }
            ++next.stateCount;
        }
        if (row != nullptr)
        {
            row->hasInitial = true;
            row->initial = child;
        }
        return next;
    }

    constexpr Hsm unwired(StateEnum s) const
    {
        Hsm next = *this;
        if (next.unwiredCount < kMaxStates)
        {
            next.unwiredStates[next.unwiredCount] = s;
        }
        ++next.unwiredCount;
        return next;
    }

    template <class EventEnum>
    constexpr Hsm<Host, StateEnum, EventEnum> on(StateEnum source, EventEnum event, StateEnum target,
                                                 void (Host::*action)() = nullptr,
                                                 bool (Host::*guard)() const = nullptr) const
    {
        return promote<EventEnum>().on(source, event, target, action, guard);
    }

    template <class EventEnum>
    constexpr Hsm<Host, StateEnum, EventEnum> local(StateEnum source, EventEnum event, StateEnum target,
                                                    void (Host::*action)() = nullptr,
                                                    bool (Host::*guard)() const = nullptr) const
    {
        return promote<EventEnum>().local(source, event, target, action, guard);
    }

    template <class EventEnum>
    constexpr Hsm<Host, StateEnum, EventEnum> internal(StateEnum source, EventEnum event, void (Host::*action)(),
                                                       bool (Host::*guard)() const = nullptr) const
    {
        return promote<EventEnum>().internal(source, event, action, guard);
    }

private:
    constexpr StateRow<StateEnum>* find(StateEnum s)
    {
        for (std::size_t i = 0; i < stateCount; ++i)
        {
            if (states[i].state == s)
            {
                return &states[i];
            }
        }
        return nullptr;
    }

    // Carry the State tree built so far into the final, Event-known table. The
    // Transition arrays start empty there; the first Transition call fills them.
    template <class EventEnum>
    constexpr Hsm<Host, StateEnum, EventEnum> promote() const
    {
        Hsm<Host, StateEnum, EventEnum> table{};
        table.states = states;
        table.stateCount = stateCount;
        table.unwiredStates = unwiredStates;
        table.unwiredCount = unwiredCount;
        return table;
    }
};

// Stage 0: the entry point `Hsm<Host>{}`, with both enums still pending. The
// first State-side call deduces the State enum (handing off to stage 1); a
// Transition call deduces both enums at once (handing off to the final table).
template <class Host>
struct Hsm<Host, Deduce, Deduce> {
    template <class StateEnum>
    constexpr Hsm<Host, StateEnum, Deduce> state(StateEnum s, StateEnum parent) const
    {
        return Hsm<Host, StateEnum, Deduce>{}.state(s, parent);
    }

    template <class StateEnum>
    constexpr Hsm<Host, StateEnum, Deduce> initial(StateEnum parent, StateEnum child) const
    {
        return Hsm<Host, StateEnum, Deduce>{}.initial(parent, child);
    }

    template <class StateEnum>
    constexpr Hsm<Host, StateEnum, Deduce> unwired(StateEnum s) const
    {
        return Hsm<Host, StateEnum, Deduce>{}.unwired(s);
    }

    template <class StateEnum, class EventEnum>
    constexpr Hsm<Host, StateEnum, EventEnum> on(StateEnum source, EventEnum event, StateEnum target,
                                                 void (Host::*action)() = nullptr,
                                                 bool (Host::*guard)() const = nullptr) const
    {
        return Hsm<Host, StateEnum, Deduce>{}.on(source, event, target, action, guard);
    }

    template <class StateEnum, class EventEnum>
    constexpr Hsm<Host, StateEnum, EventEnum> local(StateEnum source, EventEnum event, StateEnum target,
                                                    void (Host::*action)() = nullptr,
                                                    bool (Host::*guard)() const = nullptr) const
    {
        return Hsm<Host, StateEnum, Deduce>{}.local(source, event, target, action, guard);
    }

    template <class StateEnum, class EventEnum>
    constexpr Hsm<Host, StateEnum, EventEnum> internal(StateEnum source, EventEnum event, void (Host::*action)(),
                                                       bool (Host::*guard)() const = nullptr) const
    {
        return Hsm<Host, StateEnum, Deduce>{}.internal(source, event, action, guard);
    }
};

}  // namespace eta_hsm
