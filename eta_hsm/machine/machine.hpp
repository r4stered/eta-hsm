#pragma once

// Runtime machine holder for the v2 data-oriented core (issue 0002). Binds a
// `constexpr` table (built with Hsm, see hsm.hpp) as a non-type template
// argument so Dispatch is generated at compile time -- no virtual indirection,
// no heap allocation on the event path. Holds the current Leaf State and a
// default-constructed Host that Actions and per-State hooks run on.

#include <cstddef>

#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm {

namespace detail {

// Follow Initial Substates down from `s` until reaching a Leaf the machine can
// rest in. For a flat machine this resolves Top -> its Initial Substate.
template <auto Table, class StateEnum>
constexpr StateEnum resting_leaf(StateEnum s)
{
    StateEnum cur = s;
    for (;;) {
        bool advanced = false;
        for (std::size_t i = 0; i < Table.stateCount; ++i) {
            if (Table.states[i].state == cur && Table.states[i].hasInitial) {
                cur = Table.states[i].initial;
                advanced = true;
                break;
            }
        }
        if (!advanced) {
            return cur;
        }
    }
}

// The single Top State of the table.
template <auto Table, class StateEnum>
constexpr StateEnum top_state()
{
    for (std::size_t i = 0; i < Table.stateCount; ++i) {
        if (Table.states[i].isTop) {
            return Table.states[i].state;
        }
    }
    return StateEnum{};
}

// The populated prefix of the table's Transitions, as a fixed-size value the
// generated Dispatch can expand over with a `template for`.
template <auto Table>
consteval auto transitions()
{
    std::array<TransitionRow<typename decltype(Table)::State, typename decltype(Table)::Event,
                             typename decltype(Table)::HostType>,
               Table.transitionCount>
        out{};
    for (std::size_t i = 0; i < Table.transitionCount; ++i) {
        out[i] = Table.transitions[i];
    }
    return out;
}

}  // namespace detail

template <auto Table>
class Machine {
public:
    using Host = typename decltype(Table)::HostType;
    using State = typename decltype(Table)::State;
    using Event = typename decltype(Table)::Event;

    // The Leaf State the machine currently rests in.
    State identify() const { return current_; }

    // Deliver one Event. Dispatch is generated at compile time: a `template for`
    // over the table's Transitions expands to one comparison per Transition, so
    // there is no virtual indirection and no heap allocation on the event path.
    void dispatch(Event event)
    {
        static constexpr auto trs = detail::transitions<Table>();
        template for (constexpr auto tr : trs) {
            if (tr.source == current_ && tr.event == event) {
                current_ = tr.target;
                return;
            }
        }
    }

    Host& host() { return host_; }
    const Host& host() const { return host_; }

private:
    Host host_{};
    State current_{detail::resting_leaf<Table>(detail::top_state<Table, State>())};
};

}  // namespace eta_hsm
