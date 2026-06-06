#pragma once

// The compile-time backbone: the exhaustive single-step differential, lifted to
// constant evaluation. Both production dispatch and the reference interpreter are
// constant-expression evaluable, so for every reachable (State, Event) the two can
// be compared BEFORE the machine ever runs -- and a divergence becomes a
// static_assert that fails the build, naming the offending (State, Event).
//
// The result is a CtReport: a fixed-capacity literal type (no heap escaping the
// evaluation) carrying ok plus a P2741 message naming the first divergence, so it
// is usable directly as a static_assert message operand. The std::vector/std::string
// the reference and the recording observer use live entirely inside the evaluation;
// only the fixed-size report leaves it.
//
// This mirrors backbone.hpp's runtime differential -- production vs reference on the
// resting Leaf, the ordered Exit/Entry chain, and (for a log-bearing Host) the full
// Action-bearing transcript -- but stops at that differential. The nine universal
// invariants and the runtime backbone (backbone.hpp) remain the broader system of
// record; this header answers the narrower, harder question of whether the
// differential itself is tractable at compile time. It reuses backbone.hpp's
// RecordingObserver and HasLog (both constexpr-friendly) so the two differentials
// record and gate on a transcript identically.

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/reference/backbone.hpp"
#include "eta_hsm/reference/reference_interpreter.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::reference {

// The outcome of a compile-time differential: ok plus a fixed-capacity message
// naming the first divergence (empty when ok), and the count of (State, Event)
// pairs that were verified. size()/data() make it a P2741 static_assert message.
struct CtReport {
    bool ok{true};
    std::array<char, 256> text{};
    std::size_t len{0};
    std::size_t reachableLeaves{0};
    std::size_t eventCount{0};
    std::size_t pairsVerified{0};

    constexpr std::size_t size() const { return len; }
    constexpr const char* data() const { return text.data(); }

    constexpr void append(std::string_view s)
    {
        for (char c : s)
        {
            if (len + 1 < text.size())
            {
                text[len++] = c;
            }
        }
    }
};

// The enumerator identifier of `v`, or "<?>" if `v` is not a declared enumerator
// (so a message is always printable, even for a corrupted value).
template <class E>
constexpr std::string_view ctName(E v)
{
    auto const n = eta_hsm::enum_name(v);
    return n ? *n : std::string_view{"<?>"};
}

// Compare production dispatch against the reference for ONE reachable (State, Event)
// step, writing the first divergent facet (if any) into `rep` with the offending
// (State, Event) named. Returns true when the step agreed.
//
// `r.path` positions a fresh Machine at `r.leaf`; the live Host is re-seeded to
// `guardHost` before every replay Dispatch and before the measured one, exactly as
// the reference computes each edge from the pristine Host -- so production's Guard
// outcomes track the reference's. `fault` corrupts the reference plan (None = the
// faithful interpreter), the seam the planted-divergence build-failure test drives.
template <auto Table, class State, class EventEnum, class Host>
constexpr bool ctDifferentialStep(const TableView<State, EventEnum, Host>& view, const Reached<State, EventEnum>& r,
                                  EventEnum e, const Host& guardHost, Fault fault, CtReport& rep)
{
    using Obs = RecordingObserver<State>;

    auto const plan = referenceStep(view, r.leaf, e, guardHost, fault);

    std::vector<State> exits;
    std::vector<State> entries;
    Machine<Table, Obs> m{Obs{&exits, &entries}};
    for (EventEnum pe : r.path)
    {
        m.host() = guardHost;
        m.dispatch(pe);
    }
    if (m.identify() != r.leaf)
    {
        rep.ok = false;
        rep.append("replay path landed wrong at ");
        rep.append(ctName(r.leaf));
        rep.append("+");
        rep.append(ctName(e));
        return false;
    }

    m.host() = guardHost;  // the measured step reads the pristine Host too
    exits.clear();
    entries.clear();
    m.dispatch(e);

    auto fail = [&](std::string_view what) {
        rep.ok = false;
        rep.append(what);
        rep.append(" at ");
        rep.append(ctName(r.leaf));
        rep.append("+");
        rep.append(ctName(e));
    };

    if (m.identify() != plan.leaf)
    {
        fail("resting Leaf mismatch (production vs reference)");
        return false;
    }
    if (exits != plan.exits)
    {
        fail("Exit sequence mismatch (production vs reference)");
        return false;
    }
    if (entries != plan.entries)
    {
        fail("Entry sequence mismatch (production vs reference)");
        return false;
    }
    if constexpr (HasLog<Host>)
    {
        if (m.host().log != renderOn(plan).log)
        {
            fail("transcript mismatch (production vs reference)");
            return false;
        }
    }
    return true;
}

// Run the exhaustive compile-time differential over `Table`: for every reachable
// (State, Event), compare production dispatch against the reference and stop at the
// first divergence, naming it in the returned CtReport. Guards are evaluated against
// `guardHost`, which also seeds each live Machine. `fault` corrupts the reference
// plan (None = faithful); the planted-divergence build-failure test passes a fault
// to prove a real divergence fails the build.
//
// Consteval, so a `static_assert(runCompileTimeDifferential<T>().ok, ...)` proves
// the whole reachable single-step space agrees before the machine ever runs.
template <auto Table>
consteval CtReport runCompileTimeDifferential(const typename decltype(Table)::HostType& guardHost = {},
                                              Fault fault = Fault::None)
{
    using Event = typename decltype(Table)::Event;

    auto const view = makeTableView<Table>();
    constexpr auto events = eta_hsm::enum_values<Event>();

    CtReport rep;
    rep.eventCount = events.size();

    auto const reached = reachable(view, guardHost);
    rep.reachableLeaves = reached.size();

    for (auto const& r : reached)
    {
        for (Event e : events)
        {
            if (!ctDifferentialStep<Table>(view, r, e, guardHost, fault, rep))
            {
                return rep;  // first divergence wins; its (State, Event) is named
            }
            ++rep.pairsVerified;
        }
    }
    return rep;
}

}  // namespace eta_hsm::reference
