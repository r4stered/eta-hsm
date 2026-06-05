#pragma once

// Runtime machine holder for the v2 data-oriented core (issue 0002). Binds a
// `constexpr` table (built with Hsm, see hsm.hpp) as a non-type template
// argument so Dispatch is generated at compile time -- no virtual indirection,
// no heap allocation on the event path. Holds the current Leaf State and a
// default-constructed Host that Actions and per-State hooks run on.

#include <algorithm>
#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm {

namespace detail {

// A structural compile-time string, usable as a non-type template argument so a
// hook prefix ("entry"/"exit") can be passed to run_hook.
template <std::size_t N>
struct fixed_string {
    char data[N]{};
    constexpr fixed_string(const char (&s)[N]) { std::copy_n(s, N, data); }
    constexpr std::string_view view() const { return {data, N - 1}; }
};

// True if member `m` is named `<prefix>_<state>`. Builds the candidate name in a
// transient consteval allocation (never escapes), so no constexpr std::string
// has to persist.
consteval bool hook_named(std::meta::info m, std::string_view prefix, std::string_view state)
{
    std::string want = std::string(prefix) + "_" + std::string(state);
    return std::meta::has_identifier(m) && std::meta::identifier_of(m) == std::string_view{want};
}

// Call host.<Prefix>_<Name>() for the State `s`, where <Name> is s's enumerator
// identifier -- but only if the Host actually declares that member. Both the
// State match and the member detection are resolved at compile time by expanding
// over the enumerators and the Host's members (P2996 + expansion statements);
// states with no matching hook expand to nothing.
template <fixed_string Prefix, class Host, class StateEnum>
void run_hook(Host& host, StateEnum s)
{
    template for (constexpr std::meta::info ev :
                  std::define_static_array(std::meta::enumerators_of(^^StateEnum))) {
        if (s == std::meta::extract<StateEnum>(ev)) {
            constexpr std::string_view name = std::meta::identifier_of(ev);
            template for (constexpr std::meta::info m : std::define_static_array(
                              std::meta::members_of(^^Host, std::meta::access_context::current()))) {
                if constexpr (std::meta::is_function(m) && !std::meta::is_special_member_function(m)) {
                    if constexpr (hook_named(m, Prefix.view(), name)) {
                        (host.[:m:])();
                    }
                }
            }
        }
    }
}

// Index of the row describing State `s`, or Table.stateCount if `s` is not a
// declared State. The single place the table's State rows are searched.
template <auto Table, class StateEnum>
constexpr std::size_t state_index(StateEnum s)
{
    for (std::size_t i = 0; i < Table.stateCount; ++i) {
        if (Table.states[i].state == s) {
            return i;
        }
    }
    return Table.stateCount;
}

// Follow Initial Substates down from `s` until reaching a Leaf the machine can
// rest in. For a flat machine this resolves Top -> its Initial Substate.
template <auto Table, class StateEnum>
constexpr StateEnum resting_leaf(StateEnum s)
{
    StateEnum cur = s;
    for (;;) {
        std::size_t const i = state_index<Table>(cur);
        if (i == Table.stateCount || !Table.states[i].hasInitial) {
            return cur;
        }
        cur = Table.states[i].initial;
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
    // An Event the current Leaf does not handle defers up the parent chain to
    // Top; the first level with a matching Transition wins. A Composite Target is
    // forwarded to its Initial Substate so the machine rests in a Leaf.
    void dispatch(Event event)
    {
        static constexpr auto trs = detail::transitions<Table>();
        State handler = current_;
        for (;;) {
            bool found = false;
            State target{};
            void (Host::*action)() = nullptr;
            template for (constexpr auto tr : trs) {
                if (tr.source == handler && tr.event == event) {
                    target = tr.target;
                    action = tr.action;
                    found = true;
                }
            }
            if (found) {
                State const dest = detail::resting_leaf<Table>(target);
                detail::run_hook<"exit">(host_, current_);  // Exit the Leaf being left
                if (action != nullptr) {
                    (host_.*action)();  // Action runs between Exit and Entry
                }
                current_ = dest;
                detail::run_hook<"entry">(host_, current_);  // Enter the destination Leaf
                return;
            }
            if (is_top(handler)) {
                return;  // unhandled by the whole chain
            }
            handler = parent_of(handler);
        }
    }

    // True if `ancestor` is the current Leaf or one of its ancestors. Every Leaf
    // is a substate of Top.
    bool isInSubstateOf(State ancestor) const
    {
        State s = current_;
        for (;;) {
            if (s == ancestor) {
                return true;
            }
            if (is_top(s)) {
                return false;
            }
            s = parent_of(s);
        }
    }

    Host& host() { return host_; }
    const Host& host() const { return host_; }

private:
    // The parent of `s` in the State tree (Top is its own parent; the dispatch
    // loop stops at Top before consulting this).
    static State parent_of(State s)
    {
        std::size_t const i = detail::state_index<Table>(s);
        return i == Table.stateCount ? s : Table.states[i].parent;
    }

    static bool is_top(State s)
    {
        std::size_t const i = detail::state_index<Table>(s);
        return i != Table.stateCount && Table.states[i].isTop;
    }

    Host host_{};
    State current_{detail::resting_leaf<Table>(detail::top_state<Table, State>())};
};

}  // namespace eta_hsm
