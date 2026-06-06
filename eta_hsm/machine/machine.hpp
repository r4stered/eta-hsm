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
#include <string_view>
#include <utility>

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/validator.hpp"

namespace eta_hsm {

namespace detail {

// The default Machine observer: a no-op. Every notify is an empty inline
// template, so a Machine<Table> with no logging observer compiles to exactly
// what it did before the observer seam existed -- no member of substance, no
// runtime cost (issue 0007). A real observer (the auto-logging layer's
// LoggingObserver) supplies the same members with bodies.
struct NullObserver {
    template <class State>
    void onEntry(State)
    {}
    template <class State>
    void onExit(State)
    {}
    template <class State>
    void onInit(State)
    {}
    template <class State, class Event>
    void onTransition(State, State, Event)
    {}
};

// A structural compile-time string, usable as a non-type template argument so a
// hook prefix ("entry"/"exit") can be passed to run_hook.
template <std::size_t N>
struct fixed_string {
    char data[N]{};
    constexpr fixed_string(const char (&s)[N]) { std::copy_n(s, N, data); }
    constexpr std::string_view view() const { return {data, N - 1}; }
};

// True if member `m` is named `<prefix>_<state>`. Compares the identifier against
// the prefix/state pieces with pure string_view operations rather than building a
// concatenated `std::string`: a transient consteval std::string carries a pointer
// null-check that -fsanitize=undefined instruments into a non-constant expression,
// which would make the whole `if constexpr (hook_named(...))` ill-formed under the
// sanitizer job. The piecewise match needs no allocation and stays constexpr-pure.
consteval bool hook_named(std::meta::info m, std::string_view prefix, std::string_view state)
{
    if (!std::meta::has_identifier(m))
    {
        return false;
    }
    const std::string_view id = std::meta::identifier_of(m);
    return id.size() == prefix.size() + 1 + state.size() && id.substr(0, prefix.size()) == prefix &&
           id[prefix.size()] == '_' && id.substr(prefix.size() + 1) == state;
}

// Call host.<Prefix>_<Name>(args...) for the State `s`, where <Name> is s's
// enumerator identifier -- but only if the Host actually declares that member and
// it is callable with `args`. Both the State match and the member detection are
// resolved at compile time by expanding over the enumerators and the Host's
// members (P2996 + expansion statements); states with no matching hook expand to
// nothing. `args` lets the During tick forward an Input to stateUpdate_<Name>;
// entry/exit/during pass none. For the variadic stateUpdate prefix a name match
// whose arity does not fit `args` is skipped (the requires-guard), so the tick is
// never called with the wrong shape; the fixed-arity prefixes have no such guard,
// so a name-matching hook of the wrong shape is a hard compile error there.
template <fixed_string Prefix, class Host, class StateEnum, class... Args>
void run_hook(Host& host, StateEnum s, Args&&... args)
{
    template for (constexpr std::meta::info ev : std::define_static_array(std::meta::enumerators_of(^^StateEnum)))
    {
        if (s == std::meta::extract<StateEnum>(ev))
        {
            constexpr std::string_view name = std::meta::identifier_of(ev);
            template for (constexpr std::meta::info m :
                          std::define_static_array(std::meta::members_of(^^Host, std::meta::access_context::current())))
            {
                if constexpr (std::meta::is_function(m) && !std::meta::is_special_member_function(m))
                {
                    if constexpr (hook_named(m, Prefix.view(), name))
                    {
                        // Only the variadic stateUpdate tick is allowed to skip a
                        // name-matching hook whose arity does not fit the forwarded
                        // Input (the intentional skip 0005 introduced). For every
                        // fixed-arity prefix -- entry/exit/during, always called
                        // with an EMPTY pack -- a name match whose shape is wrong is
                        // a programmer error, so it must hard-error here rather than
                        // silently no-op and quietly weaken the hook contract.
                        if constexpr (Prefix.view() == std::string_view{"stateUpdate"})
                        {
                            if constexpr (requires { (host.[:m:])(std::forward<Args>(args)...); })
                            {
                                (host.[:m:])(std::forward<Args>(args)...);
                            }
                        }
                        else
                        {
                            (host.[:m:])(std::forward<Args>(args)...);
                        }
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
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].state == s)
        {
            return i;
        }
    }
    return Table.stateCount;
}

// The single Top State of the table.
template <auto Table, class StateEnum>
constexpr StateEnum top_state()
{
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].isTop)
        {
            return Table.states[i].state;
        }
    }
    return StateEnum{};
}

// True if `a` is `b` itself or one of b's ancestors (walking up to Top). The
// basis for least-common-ancestor: a State that is an ancestor-or-self of both
// Source and Target spans the chain a Transition runs.
template <auto Table, class StateEnum>
constexpr bool is_ancestor_or_self(StateEnum a, StateEnum b)
{
    StateEnum cur = b;
    for (;;)
    {
        if (cur == a)
        {
            return true;
        }
        std::size_t const i = state_index<Table>(cur);
        if (i == Table.stateCount || Table.states[i].isTop)
        {
            return false;  // reached the root (or an unknown State) without matching
        }
        cur = Table.states[i].parent;
    }
}

// The least-common-ancestor for an External Transition: the nearest strict
// ancestor of `source` that is also an ancestor-or-self of `target`. This is the
// State the chain does NOT cross -- everything below it on the Source side is
// Exited and everything below it on the Target side is Entered. For a self- or
// parent/child Transition the result is the parent (so the shared State is Exited
// and re-entered); the walk clamps at Top, so the root is never Exited.
template <auto Table, class StateEnum>
constexpr StateEnum lca_external(StateEnum source, StateEnum target)
{
    std::size_t const i = state_index<Table>(source);
    if (i == Table.stateCount || Table.states[i].isTop)
    {
        return top_state<Table, StateEnum>();
    }
    StateEnum cur = Table.states[i].parent;  // first strict ancestor of source
    for (;;)
    {
        if (is_ancestor_or_self<Table>(cur, target))
        {
            return cur;
        }
        std::size_t const j = state_index<Table>(cur);
        if (j == Table.stateCount || Table.states[j].isTop)
        {
            return top_state<Table, StateEnum>();
        }
        cur = Table.states[j].parent;
    }
}

// The least-common-ancestor for a Local Transition. When Source and Target are
// in a parent/child relationship the shared State is the ancestor itself, so it
// is neither Exited nor re-entered; for unrelated States this is identical to the
// External result.
template <auto Table, class StateEnum>
constexpr StateEnum lca_local(StateEnum source, StateEnum target)
{
    if (is_ancestor_or_self<Table>(source, target))
    {
        return source;  // Source contains Target (or Source == Target): keep Source
    }
    if (is_ancestor_or_self<Table>(target, source))
    {
        return target;  // Target contains Source: keep Target
    }
    return lca_external<Table>(source, target);
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
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        out[i] = Table.transitions[i];
    }
    return out;
}

}  // namespace detail

// `Observer` is an opt-in seam for watching the machine run: it is notified at
// the same points the Exit/Action/Entry/init chain already touches, so a logging
// layer (issue 0007) can render Transitions, Entries, Exits, and inits without
// re-deriving the traversal. It defaults to NullObserver, which compiles away --
// every existing `Machine<Table>` is unchanged.
template <auto Table, class Observer = detail::NullObserver>
class Machine {
public:
    using Host = typename decltype(Table)::HostType;
    using State = typename decltype(Table)::State;
    using Event = typename decltype(Table)::Event;

    // Validation runs automatically the moment a machine is instantiated: an
    // ill-formed table fails this static_assert with a message naming the
    // offending element (issue 0006), rather than surfacing as a deep template
    // error later. A well-formed table compiles away to nothing.
    static constexpr ValidationReport kValidation = validate<Table>();
    static_assert(kValidation.ok, kValidation);

    // On construction the machine enters its initial configuration: it runs the
    // Entry hook for Top and for each Initial Substate down to the resting Leaf,
    // so the machine never sits in a State whose Entry has not run. (v1 achieved
    // the same by kicking off a Top -> Top self-transition in its constructor.)
    Machine() { current_ = enter_initial_chain(); }

    // Same, but with a caller-supplied Observer in place. The Observer must be
    // installed before the initial Entry chain runs so it can witness the init
    // (the auto-logging layer relies on this to emit construction-time lines).
    explicit Machine(Observer observer) : observer_{std::move(observer)} { current_ = enter_initial_chain(); }

    // The Leaf State the machine currently rests in.
    State identify() const { return current_; }

    // Deliver one Event. Dispatch is generated at compile time: a `template for`
    // over the table's Transitions expands to one comparison per Transition, so
    // there is no virtual indirection and no heap allocation on the event path.
    // An Event the current Leaf does not handle -- or handles only with a Guard
    // that is currently false -- defers up the parent chain to the nearest
    // ancestor that handles it; the first matching Transition whose Guard passes
    // wins. A matched Internal Transition runs its Action and returns with no
    // State change; otherwise take_transition runs the ordered Exit/Action/Entry
    // chain through the least-common-ancestor and drills the Target into its
    // Initial Substate so the machine comes to rest in a Leaf.
    void dispatch(Event event)
    {
        static constexpr auto trs = detail::transitions<Table>();
        State const origin = current_;  // resting Leaf before this Dispatch, for the Transition line
        State handler = current_;
        for (;;)
        {
            bool found = false;
            State source{};
            State target{};
            void (Host::*action)() = nullptr;
            bool internal = false;
            bool local = false;
            // First matching Transition whose Guard passes wins. A guarded row
            // whose Guard is false is skipped, so the Event keeps deferring -- to
            // a later same-source fallback row, or up the parent chain.
            template for (constexpr auto tr : trs)
            {
                if (!found && tr.source == handler && tr.event == event && (tr.guard == nullptr || (host_.*tr.guard)()))
                {
                    source = tr.source;
                    target = tr.target;
                    action = tr.action;
                    internal = tr.internal;
                    local = tr.local;
                    found = true;
                }
            }
            if (found)
            {
                if (internal)
                {
                    // Internal Transition: run the Action only. No Exit/Entry, no
                    // State change -- the machine rests where it already was.
                    if (action != nullptr)
                    {
                        (host_.*action)();
                    }
                    return;
                }
                take_transition(source, target, action, local);
                // The Transition line is leaf-to-leaf and fires last, after the
                // Exit/Entry/init chain that take_transition ran.
                observer_.onTransition(origin, current_, event);
                return;
            }
            if (is_top(handler))
            {
                return;  // unhandled by the whole chain
            }
            handler = parent_of(handler);
        }
    }

    // Run the During tick for the current Leaf State, independent of any Event:
    // call the Host's during_<Name>() hook for the State the machine rests in, if
    // it declares one. No Transition runs and no Exit/Entry fires -- the tick
    // touches only the current Leaf, never an ancestor. A Leaf whose Host has no
    // during hook does nothing.
    void during() { detail::run_hook<"during">(host_, current_); }

    // The input-consuming During tick: call the Host's stateUpdate_<Name>(input)
    // hook for the current Leaf, forwarding `input`. Like during() it runs only
    // the current Leaf's hook with no Transition; a Leaf whose Host has no
    // matching stateUpdate hook (or whose hook does not accept this input) does
    // nothing. `Input` is deduced, so the Host names whatever parameter type fits.
    template <class Input>
    void during(const Input& input)
    {
        detail::run_hook<"stateUpdate">(host_, current_, input);
    }

    // True if `ancestor` is the current Leaf or one of its ancestors. Every Leaf
    // is a substate of Top.
    bool isInSubstateOf(State ancestor) const
    {
        State s = current_;
        for (;;)
        {
            if (s == ancestor)
            {
                return true;
            }
            if (is_top(s))
            {
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

    // Run a non-Internal Transition end to end. `source` is the State whose row
    // matched (the current Leaf or an ancestor it deferred to) and `target` the
    // row's Target. The least-common-ancestor bounds the chain: Exit from the
    // current Leaf up to the LCA (exclusive, bottom-up), then the Action, then
    // Entry from the LCA down to the Target (exclusive of LCA, top-down), then
    // drill the Target into its Initial Substates so the machine rests in a Leaf.
    // External (the default) Exits and re-enters a shared parent/child State;
    // Local does not -- the only difference is which State the LCA resolves to.
    void take_transition(State source, State target, void (Host::*action)(), bool local)
    {
        State const lca =
            local ? detail::lca_local<Table>(source, target) : detail::lca_external<Table>(source, target);

        // Exit the active States from the current Leaf up to the LCA, bottom-up.
        for (State s = current_; s != lca; s = parent_of(s))
        {
            detail::run_hook<"exit">(host_, s);
            observer_.onExit(s);
        }

        if (action != nullptr)
        {
            (host_.*action)();  // Action runs between Exit and Entry
        }

        // Entry from the LCA down to the Target: collect the path bottom-up, then
        // fire each Entry hook top-down (the order States are actually entered).
        State path[kMaxStates];
        std::size_t depth = 0;
        for (State s = target; s != lca; s = parent_of(s))
        {
            path[depth++] = s;
        }
        while (depth-- > 0)
        {
            detail::run_hook<"entry">(host_, path[depth]);
            observer_.onEntry(path[depth]);
        }
        // The Target is initialized once it has been entered, before drilling into
        // its Initial Substates -- this is the init seam v1 logged for the Target
        // (it fires even when the Target stayed active and was not re-entered).
        observer_.onInit(target);

        // Drill the Target into its Initial Substates until a Leaf, firing each
        // State's Entry hook on the way down. This is where a Composite Target
        // settles in the Leaf the machine comes to rest in.
        State leaf = target;
        for (;;)
        {
            std::size_t const i = detail::state_index<Table>(leaf);
            if (i == Table.stateCount || !Table.states[i].hasInitial)
            {
                break;
            }
            leaf = Table.states[i].initial;
            detail::run_hook<"entry">(host_, leaf);
            observer_.onEntry(leaf);
            observer_.onInit(leaf);
        }
        current_ = leaf;
    }

    // Walk from Top down the Initial Substate chain, running each State's Entry
    // hook, and return the resting Leaf. Used once, at construction.
    State enter_initial_chain()
    {
        State s = detail::top_state<Table, State>();
        detail::run_hook<"entry">(host_, s);
        observer_.onEntry(s);
        observer_.onInit(s);
        for (;;)
        {
            std::size_t const i = detail::state_index<Table>(s);
            if (i == Table.stateCount || !Table.states[i].hasInitial)
            {
                return s;
            }
            s = Table.states[i].initial;
            detail::run_hook<"entry">(host_, s);
            observer_.onEntry(s);
            observer_.onInit(s);
        }
    }

    Host host_{};
    State current_{};
    Observer observer_{};
};

}  // namespace eta_hsm
