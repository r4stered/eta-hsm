#pragma once

// A naive reference interpreter for HSM dispatch: the executable oracle the
// production Machine is differential-tested against. It walks a plain runtime
// view of a machine table directly -- finds the matching Transition by walking up
// the parent chain, evaluates the Guard, computes the least-common-ancestor, and
// derives the ordered Exit/Action/Entry chain and the resting Leaf by plain
// tree-walking. It is deliberately simple and standalone: it never routes through
// the production dispatch (no `template for`, no structural-NTTP tables), so a
// disagreement between the two pins a real behavioral bug.
//
// It is written against a runtime TableView (a thin adapter over the `constexpr`
// table, which is readable at runtime), not the table's non-type template
// parameter, so the same code can later interpret runtime-generated machines.

#include <cstddef>
#include <utility>
#include <vector>

#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::reference {

// A plain runtime copy of a machine table's populated prefix. The interpreter
// walks THIS, never the NTTP table type, so it stays reusable for runtime tables.
template <class StateEnum, class EventEnum, class Host>
struct TableView {
    struct State {
        StateEnum state{};
        StateEnum parent{};
        bool isTop{false};
        bool hasInitial{false};
        StateEnum initial{};
    };
    struct Transition {
        StateEnum source{};
        EventEnum event{};
        StateEnum target{};
        void (Host::*action)() = nullptr;
        bool (Host::*guard)() const = nullptr;
        bool internal{false};
        bool local{false};
    };

    std::vector<State> states;
    std::vector<Transition> transitions;

    // The row describing `s`, or nullptr if `s` is not a declared State.
    const State* find(StateEnum s) const
    {
        for (const State& row : states)
        {
            if (row.state == s)
            {
                return &row;
            }
        }
        return nullptr;
    }

    StateEnum parentOf(StateEnum s) const
    {
        const State* row = find(s);
        return row == nullptr ? s : row->parent;
    }

    bool isTop(StateEnum s) const
    {
        const State* row = find(s);
        return row != nullptr && row->isTop;
    }

    StateEnum topState() const
    {
        for (const State& row : states)
        {
            if (row.isTop)
            {
                return row.state;
            }
        }
        return StateEnum{};
    }

    // True if `a` is `b` itself or one of b's ancestors, walking up to Top.
    bool isAncestorOrSelf(StateEnum a, StateEnum b) const
    {
        StateEnum cur = b;
        for (;;)
        {
            if (cur == a)
            {
                return true;
            }
            const State* row = find(cur);
            if (row == nullptr || row->isTop)
            {
                return false;
            }
            cur = row->parent;
        }
    }

    // The least-common-ancestor for an External Transition: the nearest strict
    // ancestor of `source` that is also an ancestor-or-self of `target`. A
    // Top-sourced Transition returns Top itself (the caller re-enters Top).
    StateEnum lcaExternal(StateEnum source, StateEnum target) const
    {
        const State* row = find(source);
        if (row == nullptr || row->isTop)
        {
            return topState();
        }
        StateEnum cur = row->parent;
        for (;;)
        {
            if (isAncestorOrSelf(cur, target))
            {
                return cur;
            }
            const State* curRow = find(cur);
            if (curRow == nullptr || curRow->isTop)
            {
                return topState();
            }
            cur = curRow->parent;
        }
    }

    // The least-common-ancestor for a Local Transition: when Source and Target
    // are in a parent/child relationship the shared State is kept (neither Exited
    // nor re-entered); otherwise identical to the External result.
    StateEnum lcaLocal(StateEnum source, StateEnum target) const
    {
        if (isAncestorOrSelf(source, target))
        {
            return source;
        }
        if (isAncestorOrSelf(target, source))
        {
            return target;
        }
        return lcaExternal(source, target);
    }
};

// Build a runtime view of any `constexpr` Hsm table value.
template <auto Table>
auto makeTableView()
{
    using StateEnum = typename decltype(Table)::State;
    using EventEnum = typename decltype(Table)::Event;
    using Host = typename decltype(Table)::HostType;
    TableView<StateEnum, EventEnum, Host> view;
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        auto const& r = Table.states[i];
        view.states.push_back({r.state, r.parent, r.isTop, r.hasInitial, r.initial});
    }
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        auto const& t = Table.transitions[i];
        view.transitions.push_back({t.source, t.event, t.target, t.action, t.guard, t.internal, t.local});
    }
    return view;
}

// A deliberately wrong reference step, for proving the differential bites. None
// is the faithful interpreter; the other modes corrupt one facet of the plan.
enum class Fault {
    None,
    DropFirstExit,  // omit the first Exit -- a wrong Exit/Entry transcript
    WrongLeaf,  // come to rest in the wrong Leaf
};

// The single-step plan: the resting Leaf plus the ordered States whose Exit and
// Entry hooks run, in execution order (Exits bottom-up, Entries top-down), and
// the Action that runs between them. `matched` is false when no Transition (on
// the Leaf or any ancestor) handles the Event -- a strict no-op.
template <class StateEnum, class Host>
struct Plan {
    bool matched{false};
    StateEnum leaf{};
    std::vector<StateEnum> exits;
    void (Host::*action)() = nullptr;
    std::vector<StateEnum> entries;
};

// Compute the single-step plan for dispatching `event` while resting in `start`.
// `guardHost` is consulted to evaluate Guards (exactly as production reads its
// Host). Pure tree-walking over the runtime view; never touches production
// dispatch.
template <class StateEnum, class EventEnum, class Host>
Plan<StateEnum, Host> referenceStep(const TableView<StateEnum, EventEnum, Host>& view, StateEnum start, EventEnum event,
                                    const Host& guardHost, Fault fault = Fault::None)
{
    Plan<StateEnum, Host> plan;
    plan.leaf = start;

    // Find the first matching Transition, deferring up the parent chain. A guarded
    // row whose Guard is false is skipped, so the Event keeps deferring.
    StateEnum handler = start;
    const typename TableView<StateEnum, EventEnum, Host>::Transition* match = nullptr;
    for (;;)
    {
        for (auto const& tr : view.transitions)
        {
            if (tr.source == handler && tr.event == event &&
                (tr.guard == nullptr || (guardHost.*(tr.guard))()))
            {
                match = &tr;
                break;
            }
        }
        if (match != nullptr)
        {
            break;
        }
        if (view.isTop(handler))
        {
            return plan;  // unhandled by the whole chain: strict no-op
        }
        handler = view.parentOf(handler);
    }

    plan.matched = true;
    plan.action = match->action;
    if (match->internal)
    {
        return plan;  // Internal Transition: Action only, no Exit/Entry, no move
    }

    StateEnum const source = match->source;
    StateEnum const target = match->target;
    StateEnum const lca = match->local ? view.lcaLocal(source, target) : view.lcaExternal(source, target);
    bool const reenterRoot = !match->local && view.isTop(source);

    // Exit from the current Leaf up to the LCA, bottom-up. The LCA is normally
    // spanned (not Exited); when re-entering the root it is the last State Exited.
    for (StateEnum s = start;; s = view.parentOf(s))
    {
        bool const atLca = (s == lca);
        if (atLca && !reenterRoot)
        {
            break;
        }
        plan.exits.push_back(s);
        if (atLca)
        {
            break;
        }
    }

    // Entry from the LCA down to the Target: collect bottom-up, then reverse to
    // the order States are actually entered (top-down).
    std::vector<StateEnum> entryPath;
    for (StateEnum s = target;; s = view.parentOf(s))
    {
        bool const atLca = (s == lca);
        if (atLca && !reenterRoot)
        {
            break;
        }
        entryPath.push_back(s);
        if (atLca)
        {
            break;
        }
    }
    for (std::size_t i = entryPath.size(); i-- > 0;)
    {
        plan.entries.push_back(entryPath[i]);
    }

    // Drill the Target into its Initial Substates until a Leaf, entering each.
    StateEnum leaf = target;
    for (;;)
    {
        const auto* row = view.find(leaf);
        if (row == nullptr || !row->hasInitial)
        {
            break;
        }
        leaf = row->initial;
        plan.entries.push_back(leaf);
    }
    plan.leaf = leaf;

    // Fault injection: corrupt one facet of the otherwise-faithful plan.
    if (fault == Fault::DropFirstExit && !plan.exits.empty())
    {
        plan.exits.erase(plan.exits.begin());
    }
    else if (fault == Fault::WrongLeaf)
    {
        plan.leaf = view.topState();
    }

    return plan;
}

// The resting Leaf the machine settles in from its initial configuration: drill
// from Top down the Initial Substate chain until a Leaf.
template <class StateEnum, class EventEnum, class Host>
StateEnum initialLeaf(const TableView<StateEnum, EventEnum, Host>& view)
{
    StateEnum s = view.topState();
    for (;;)
    {
        const auto* row = view.find(s);
        if (row == nullptr || !row->hasInitial)
        {
            return s;
        }
        s = row->initial;
    }
}

// A reachable resting Leaf paired with a replay path: the Event sequence that
// drives the machine from its initial Leaf to this one. Used to drive the live
// Machine to each reachable State for the differential (the live Machine holds
// internal state and cannot be teleported).
template <class StateEnum, class EventEnum>
struct Reached {
    StateEnum leaf{};
    std::vector<EventEnum> path;
};

// Breadth-first enumeration of every reachable resting Leaf from the initial Leaf,
// using the reference to compute each single step. Guards are evaluated against
// `guardHost`, so the reachable set reflects that Host's Guard outcomes. A
// State no Transition can reach (e.g. an orphaned Leaf) never appears.
template <class StateEnum, class EventEnum, class Host>
std::vector<Reached<StateEnum, EventEnum>> reachable(const TableView<StateEnum, EventEnum, Host>& view,
                                                     const Host& guardHost)
{
    constexpr auto events = eta_hsm::enum_values<EventEnum>();
    std::vector<Reached<StateEnum, EventEnum>> out;
    out.push_back({initialLeaf(view), {}});
    auto seen = [&](StateEnum s) {
        for (auto const& r : out)
        {
            if (r.leaf == s)
            {
                return true;
            }
        }
        return false;
    };
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        StateEnum const s = out[i].leaf;
        std::vector<EventEnum> const path = out[i].path;  // copy: out may reallocate below
        for (EventEnum e : events)
        {
            auto const plan = referenceStep(view, s, e, guardHost);
            if (!plan.matched || seen(plan.leaf))
            {
                continue;
            }
            std::vector<EventEnum> next = path;
            next.push_back(e);
            out.push_back({plan.leaf, std::move(next)});
        }
    }
    return out;
}

// Replay a plan's Exit/Action/Entry chain onto a fresh Host and return it, so the
// caller can read whatever transcript the Host records (cd_player's Player records
// into `.log`). This renders the plan's observable effects faithfully -- the same
// Entry/Exit hook splicing production uses -- but is driven by the independently
// derived plan, not by production's dispatch.
//
// The Host starts fresh, so the rendered transcript reflects only this single
// step. This faithfully matches production when each Entry/Exit hook and Action
// renders the same effect regardless of Host state accumulated before the step
// (as cd_player's do). A Host whose hook output depends on earlier mutations would
// need the same starting state production reached, threaded in here.
template <class StateEnum, class Host>
Host renderOn(const Plan<StateEnum, Host>& plan)
{
    Host host{};
    for (StateEnum s : plan.exits)
    {
        eta_hsm::detail::run_hook<"exit">(host, s);
    }
    if (plan.action != nullptr)
    {
        (host.*(plan.action))();
    }
    for (StateEnum s : plan.entries)
    {
        eta_hsm::detail::run_hook<"entry">(host, s);
    }
    return host;
}

}  // namespace eta_hsm::reference
