#pragma once

// The exhaustive BFS backbone: a generic, table-parameterized coverage engine
// built on the reference interpreter (reference_interpreter.hpp). It enumerates
// every reachable resting Leaf (the reference's reachability graph), and for every
// reachable (State, Event) pair it drives a live Machine to the State -- by
// replaying a path the reference computed -- dispatches the Event, and asserts the
// production Machine agrees with the reference on the resting Leaf and the ordered
// Exit/Entry chain. The result is a coverage *guarantee*: every reachable single
// step verified, reported as a count.
//
// Alongside the differential it asserts the nine universal HSM invariants (see
// invariantFailures), independent of the oracle for the safety-net ones. The
// per-step check is a pure function over a StepObservation, so it can be exercised
// directly with a hand-corrupted observation to prove each invariant bites.
//
// The comparison is structural -- a RecordingObserver captures the exact ordered
// States production Exits and Enters -- so the backbone is generic over any Host,
// including one with no log member at all. It never routes through production
// dispatch to derive its expectations; those come from the independent reference.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/reference/reference_interpreter.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::reference {

// A Machine Observer that records the ordered States production Exits and Enters
// into caller-owned buffers. The Machine stores the Observer by value, so this
// holds pointers back to the buffers (the auto-logging layer's LoggingObserver
// uses the same pointer-back idiom) -- the recorded order survives to be compared
// against the reference plan after a Dispatch. Every member is constexpr so the
// same observer records a Dispatch run inside constant evaluation (the compile-time
// differential, compile_time_backbone.hpp, reuses it).
template <class State>
struct RecordingObserver {
    std::vector<State>* exits{nullptr};
    std::vector<State>* entries{nullptr};

    constexpr void onExit(State s)
    {
        if (exits != nullptr)
        {
            exits->push_back(s);
        }
    }
    constexpr void onEntry(State s)
    {
        if (entries != nullptr)
        {
            entries->push_back(s);
        }
    }
    template <class S>
    constexpr void onInit(S)
    {}
    template <class S, class E>
    constexpr void onTransition(S, S, E)
    {}
};

// A Host that records an Action/Entry/Exit transcript into a `log` member (the
// example machines do). For such a Host the backbone layers a full Action-bearing
// transcript comparison on top of the structural Exit/Entry check; an empty Host
// (e.g. a synthetic probe machine) is checked structurally only.
template <class Host>
concept HasLog = requires(Host h) { h.log; };

// A readable "<State>+<Event>" tag for failure messages.
template <class State, class Event>
std::string stepLabel(State s, Event e)
{
    auto const sn = eta_hsm::enum_name(s);
    auto const en = eta_hsm::enum_name(e);
    return std::string{sn ? *sn : "?"} + "+" + std::string{en ? *en : "?"};
}

// True if `s` is a Leaf State of the view: a declared State no other State has as
// its parent. The machine must only ever rest in a Leaf.
template <class State, class EventEnum, class Host>
bool isLeaf(const TableView<State, EventEnum, Host>& view, State s)
{
    if (view.find(s) == nullptr)
    {
        return false;
    }
    for (auto const& row : view.states)
    {
        if (!row.isTop && row.parent == s)
        {
            return false;  // `s` has a child, so it is Composite
        }
    }
    return true;
}

// True if the ancestry of `leaf` is a single chain that reaches Top without a
// cycle -- the active configuration is one root-to-leaf path, never a forest or
// a loop.
template <class State, class EventEnum, class Host>
bool isSingleRootChain(const TableView<State, EventEnum, Host>& view, State leaf)
{
    std::vector<State> seen;
    State cur = leaf;
    for (std::size_t i = 0; i <= view.states.size(); ++i)
    {
        for (State p : seen)
        {
            if (p == cur)
            {
                return false;  // cycle
            }
        }
        seen.push_back(cur);
        const auto* row = view.find(cur);
        if (row == nullptr)
        {
            return false;
        }
        if (row->isTop)
        {
            return true;
        }
        cur = row->parent;
    }
    return false;  // ran past the State count without reaching Top
}

// Everything observable about one production single-step, gathered so the nine
// invariants can be checked as a pure function. The reference fields are the
// oracle's expectation for the same step; the prod fields are what the live
// Machine actually did.
template <class State, class EventEnum, class Host>
struct StepObservation {
    State start{};
    EventEnum event{};
    bool replayLanded{false};  // the replay path drove the live Machine to `start`

    // Reference (oracle) expectation for this single step.
    bool refMatched{false};
    State refLeaf{};
    std::vector<State> refExits;
    std::vector<State> refEntries;

    // Production's observed behavior for this single step.
    State prodLeaf{};
    std::vector<State> prodExits;
    std::vector<State> prodEntries;

    // A second, independent production run of the identical step (determinism).
    State prodLeaf2{};
    std::vector<State> prodExits2;
    std::vector<State> prodEntries2;
    std::string prodLog2;

    // A During tick run after the step settled: the resting Leaf afterwards and the
    // count of Exit/Entry the observer saw during the tick (both must show inertness).
    State afterDuringLeaf{};
    std::size_t duringActivity{0};

    // isInSubstateOf for every declared State, as production reports it after settling.
    std::vector<std::pair<State, bool>> substateOf;

    // The full Action-bearing transcript, captured only when the Host records one;
    // prod is production's single-step log, ref is the reference plan rendered fresh.
    bool hasLog{false};
    std::string prodLog;
    std::string refLog;
};

// Drive one reachable (State, Event) step on the live Machine and gather a full
// StepObservation. Replays `r.path` to position the Machine at `r.leaf`, sets the
// Host to `guardHost` so production Guard outcomes match the reference, Dispatches
// `e` under a RecordingObserver, then probes the During tick, isInSubstateOf, and a
// second identical run. The reference plan is computed independently for the same step.
template <auto Table, class State, class EventEnum, class Host>
StepObservation<State, EventEnum, Host> observeStep(const TableView<State, EventEnum, Host>& view,
                                                    const Reached<State, EventEnum>& r, EventEnum e,
                                                    const Host& guardHost)
{
    using Obs = RecordingObserver<State>;
    StepObservation<State, EventEnum, Host> obs;
    obs.start = r.leaf;
    obs.event = e;

    auto const plan = referenceStep(view, r.leaf, e, guardHost);
    obs.refMatched = plan.matched;
    obs.refLeaf = plan.leaf;
    obs.refExits = plan.exits;
    obs.refEntries = plan.entries;

    // The reference's reachability graph computes every edge from the pristine
    // guardHost, so production must read that same pristine Host at every step to
    // follow the same path -- otherwise an Action that mutates a Guard-backing field
    // would steer production off the reference-derived route. Re-seeding before each
    // Dispatch keeps the live Machine's Guard outcomes identical to the reference's.
    std::vector<State> exits;
    std::vector<State> entries;
    Machine<Table, Obs> m{Obs{&exits, &entries}};
    for (EventEnum pe : r.path)
    {
        m.host() = guardHost;
        m.dispatch(pe);
    }
    obs.replayLanded = (m.identify() == r.leaf);

    m.host() = guardHost;  // the measured step reads the pristine Host too
    exits.clear();
    entries.clear();
    m.dispatch(e);
    obs.prodLeaf = m.identify();
    obs.prodExits = exits;
    obs.prodEntries = entries;
    if constexpr (HasLog<Host>)
    {
        obs.hasLog = true;
        obs.prodLog = m.host().log;
        obs.refLog = renderOn(plan).log;  // the reference plan rendered onto a fresh Host
    }

    // During inertness: a tick must fire no Transition (no Leaf change, no Exit/Entry).
    exits.clear();
    entries.clear();
    m.during();
    obs.afterDuringLeaf = m.identify();
    obs.duringActivity = exits.size() + entries.size();

    // isInSubstateOf over every declared State, after the step settled.
    for (auto const& row : view.states)
    {
        obs.substateOf.emplace_back(row.state, m.isInSubstateOf(row.state));
    }

    // Determinism: an identical, independent run must reproduce the step exactly --
    // resting Leaf, Exit/Entry chain, and (when recorded) the full transcript.
    std::vector<State> exits2;
    std::vector<State> entries2;
    Machine<Table, Obs> m2{Obs{&exits2, &entries2}};
    for (EventEnum pe : r.path)
    {
        m2.host() = guardHost;
        m2.dispatch(pe);
    }
    m2.host() = guardHost;
    exits2.clear();
    entries2.clear();
    m2.dispatch(e);
    obs.prodLeaf2 = m2.identify();
    obs.prodExits2 = exits2;
    obs.prodEntries2 = entries2;
    if constexpr (HasLog<Host>)
    {
        obs.prodLog2 = m2.host().log;
    }

    return obs;
}

// The nine universal HSM invariants, as a pure check over one StepObservation.
// Returns one descriptive failure per violated invariant (empty == all hold).
// Invariants 1-6 are oracle-independent safety nets; 7-9 corroborate the
// differential. (Invariant 3 -- no crash/UB -- is enforced by the act of running
// under the ASan/UBSan CI job, not by an assertion here.)
template <class State, class EventEnum, class Host>
std::vector<std::string> invariantFailures(const TableView<State, EventEnum, Host>& view,
                                           const StepObservation<State, EventEnum, Host>& obs, std::size_t budget)
{
    std::vector<std::string> f;
    std::string const tag = stepLabel(obs.start, obs.event);
    auto at = [&](const std::string& what) { return what + " at " + tag; };

    // (8) The Exit/Entry shape and resting Leaf must match the independently derived
    // reference plan -- the reference is the oracle for the LCA-bounded chain, so
    // agreement with it corroborates the shape rather than re-deriving it here.
    if (obs.prodLeaf != obs.refLeaf)
    {
        f.push_back(at("resting Leaf mismatch (production vs reference)"));
    }
    if (obs.prodExits != obs.refExits)
    {
        f.push_back(at("Exit sequence mismatch (production vs reference)"));
    }
    if (obs.prodEntries != obs.refEntries)
    {
        f.push_back(at("Entry sequence mismatch (production vs reference)"));
    }
    if (obs.hasLog && obs.prodLog != obs.refLog)
    {
        f.push_back(at("transcript mismatch (production vs reference)"));
    }

    // (1) The machine rests in a Leaf.
    if (!isLeaf(view, obs.prodLeaf))
    {
        f.push_back(at("rests in a non-Leaf State"));
    }

    // (2) Termination under a step budget: one Dispatch's Exit+Entry work is bounded.
    if (obs.prodExits.size() + obs.prodEntries.size() > budget)
    {
        f.push_back(at("step exceeded the Exit/Entry budget"));
    }

    // (4) An unhandled Event is a strict no-op: no Leaf change, no Exit/Entry.
    if (!obs.refMatched)
    {
        if (obs.prodLeaf != obs.start)
        {
            f.push_back(at("unhandled Event changed the resting Leaf"));
        }
        if (!obs.prodExits.empty() || !obs.prodEntries.empty())
        {
            f.push_back(at("unhandled Event ran an Exit/Entry"));
        }
    }

    // (5) isInSubstateOf is true exactly for the States that are ancestors-or-self
    // of the resting Leaf -- the active root-to-leaf path -- and false elsewhere.
    for (auto const& [a, prod] : obs.substateOf)
    {
        bool const expected = view.isAncestorOrSelf(a, obs.prodLeaf);
        if (prod != expected)
        {
            auto const an = eta_hsm::enum_name(a);
            f.push_back(at("isInSubstateOf(" + std::string{an ? *an : "?"} + ") inconsistent"));
        }
    }

    // (7) The active configuration is a single root-to-leaf chain (no cycle, no forest).
    if (!isSingleRootChain(view, obs.prodLeaf))
    {
        f.push_back(at("active path is not a single root-to-leaf chain"));
    }

    // (6) During inertness: a During tick fires no Transition.
    if (obs.afterDuringLeaf != obs.prodLeaf)
    {
        f.push_back(at("During tick changed the resting Leaf"));
    }
    if (obs.duringActivity != 0)
    {
        f.push_back(at("During tick ran an Exit/Entry"));
    }

    // (9) Determinism: an identical, independent run reproduces the step exactly,
    // including the transcript when the Host records one.
    if (obs.prodLeaf2 != obs.prodLeaf || obs.prodExits2 != obs.prodExits || obs.prodEntries2 != obs.prodEntries ||
        (obs.hasLog && obs.prodLog2 != obs.prodLog))
    {
        f.push_back(at("step is non-deterministic"));
    }

    return f;
}

// The outcome of an exhaustive backbone run: the size of the reachable space and a
// coverage count, plus any differential/invariant failures (empty == all good).
struct BackboneReport {
    std::size_t reachableLeaves{0};
    std::size_t eventCount{0};
    std::size_t pairsVerified{0};  // reachable Leaves x Events == reachableLeaves * eventCount
    std::vector<std::string> failures;

    bool ok() const { return failures.empty(); }
};

// Run the exhaustive backbone over `Table`. For every reachable (State, Event):
// drive a live Machine to the State by replaying a reference-computed path, gather
// a StepObservation, and assert the differential (production vs reference) plus the
// nine universal invariants. Guards are evaluated against `guardHost`, which also
// seeds each live Machine's Host so production Guard outcomes match the reference.
template <auto Table>
BackboneReport runBackbone(const typename decltype(Table)::HostType& guardHost = {})
{
    using Event = typename decltype(Table)::Event;

    auto const view = makeTableView<Table>();
    constexpr auto events = eta_hsm::enum_values<Event>();

    BackboneReport rep;
    rep.eventCount = events.size();

    auto const reached = reachable(view, guardHost);
    rep.reachableLeaves = reached.size();

    // A safety bound on one step's Exit+Entry work: each side of the chain visits a
    // State at most once, so the total cannot exceed twice the State count. A
    // runaway chain (the failure this guards against) would blow far past it.
    std::size_t const budget = 2 * view.states.size();

    for (auto const& r : reached)
    {
        for (Event e : events)
        {
            auto const obs = observeStep<Table>(view, r, e, guardHost);
            if (!obs.replayLanded)
            {
                rep.failures.push_back("replay path landed wrong at " + stepLabel(r.leaf, e));
                continue;
            }
            for (auto& fail : invariantFailures(view, obs, budget))
            {
                rep.failures.push_back(std::move(fail));
            }
            ++rep.pairsVerified;
        }
    }
    return rep;
}

}  // namespace eta_hsm::reference
