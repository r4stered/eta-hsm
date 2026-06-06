#pragma once

// Synthetic machine generator for the compile-time scaling probe.
//
// The PRD (0001) flags one primary risk: the v2 core leans on P2996 reflection
// and P1306 expansion statements, which are compile-time-heavy -- and the spike
// is only 7 States while a large machine is 45. This generator exists to *measure* that
// risk: it emits a valid N-State machine table at compile time so a build can be
// timed for a range of N (and hierarchy depth) spanning 7 -> 45 and a little
// beyond. See tools/scaling_probe.sh for the sweep harness.
//
// This is a compile-time `constexpr` generator, not source emission:
// `generate<N, Depth>()` returns an Hsm table value, instantiated as
// `Machine<generate<N, Depth>()>` and swept by recompiling with -DPROBE_N=… .
// No `.cpp` emission and no Python -- it reuses the same `Hsm{}` builder and
// `enum_reflection` the real machines use, so the subjects it produces double as
// free large-scale correctness subjects for a future differential-testing harness.

#include <array>
#include <cstddef>

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::probe {

// A fixed-capacity State enum spanning the probe's sweep range. Sized at the
// builder's kMaxStates (64) so `generate<N>` can wire any N up to capacity; the
// first N enumerators (S0 = Top) become the machine's States and the rest are
// marked .unwired so the validator's exhaustiveness check still passes. The enum
// is fixed-size on purpose: reflection over its enumerators (the run_hook hooks)
// is a constant baseline across the sweep, so the curve's slope isolates the
// per-machine costs that scale with N (the P1306 dispatch expansion, the
// validator's O(transitions^2) checks).
enum class ProbeState {
    S0,
    S1,
    S2,
    S3,
    S4,
    S5,
    S6,
    S7,
    S8,
    S9,
    S10,
    S11,
    S12,
    S13,
    S14,
    S15,
    S16,
    S17,
    S18,
    S19,
    S20,
    S21,
    S22,
    S23,
    S24,
    S25,
    S26,
    S27,
    S28,
    S29,
    S30,
    S31,
    S32,
    S33,
    S34,
    S35,
    S36,
    S37,
    S38,
    S39,
    S40,
    S41,
    S42,
    S43,
    S44,
    S45,
    S46,
    S47,
    S48,
    S49,
    S50,
    S51,
    S52,
    S53,
    S54,
    S55,
    S56,
    S57,
    S58,
    S59,
    S60,
    S61,
    S62,
    S63,
};

// A small Event enum. Each generated State carries kTransPerState Transitions on
// distinct Events, so this only needs to be at least that wide; the extra Events
// give the topology a little variety and a Top-level catch-all Event.
enum class ProbeEvent { E0, E1, E2, E3, E4, E5, E6, E7 };

// The Host for a probe machine: pure topology, no Actions or per-State hooks.
// Reflection over its (empty) member set is what run_hook expands against, so
// keeping it empty makes the measured cost purely about table size and dispatch.
struct ProbeHost {};

// Transitions emitted per non-Top State. Kept <= the Event count so each State's
// Transitions use distinct Events and never collide on (Source, Event).
inline constexpr std::size_t kTransPerState = 3;

// Build a valid synthetic machine of `N` States arranged as a tree approximately
// `Depth` levels below Top. Returns the Hsm table value (usable as the NTTP of
// `Machine<...>`). Requires N >= 2 (a lone Top cannot be declared through the
// builder) and Depth >= 1; the sweep starts at 7 States.
//
// Topology: S0 is Top. Node i's parent is node floor((i-1)/fanout), forming a
// complete `fanout`-ary tree whose `fanout` is the smallest branching that fits
// N nodes within `Depth` levels (Depth == 1 degenerates to a star -- every State
// a direct child of Top). Every Composite node gets its first child as Initial
// Substate. Each non-Top State carries kTransPerState Transitions onto distinct
// Events, plus one Top-level catch-all, so dispatch and parent deferral are
// exercised. Unused enumerators (S_N .. S63) are marked .unwired.
template <std::size_t N, std::size_t Depth>
consteval auto generate()
{
    static_assert(N >= 2, "probe machines need at least a Top and one Leaf");
    static_assert(N <= kMaxStates, "N exceeds the builder's State capacity");
    static_assert(Depth >= 1, "Depth must be at least 1 (a star)");

    using S = ProbeState;
    using E = ProbeEvent;
    constexpr auto kStates = enum_values<S>();  // index i -> Si
    constexpr auto kEvents = enum_values<E>();
    constexpr std::size_t numEvents = kEvents.size();
    static_assert(kTransPerState <= numEvents, "not enough Events for distinct per-State Transitions");

    // Smallest fanout f such that a complete f-ary tree of height `Depth` (Depth
    // edges from the root) holds at least N nodes: nodes(f) = 1 + f + ... + f^D.
    // Clamps at f > N so the loop always terminates.
    std::size_t fanout = 1;
    for (std::size_t f = 1;; ++f)
    {
        std::size_t nodes = 1;
        std::size_t term = 1;
        for (std::size_t d = 0; d < Depth; ++d)
        {
            term *= f;
            nodes += term;
        }
        if (nodes >= N || f > N)
        {
            fanout = f;
            break;
        }
    }

    Hsm<ProbeHost, S, E> m{};

    // Register Top (S0) and its Initial Substate (S1). .initial declares Top as
    // the isTop root; the remaining nodes attach via .state below.
    m = m.initial(kStates[0], kStates[1]);

    // Attach nodes 1..N-1 under their tree parents.
    for (std::size_t i = 1; i < N; ++i)
    {
        std::size_t const parentIdx = (i - 1) / fanout;
        m = m.state(kStates[i], kStates[parentIdx]);
    }

    // Give every Composite node (one with at least one child) an Initial Substate
    // -- its first child. Node p's children occupy indices [p*fanout+1, ...], so
    // p is Composite exactly when p*fanout+1 < N. Top (p == 0) is already done.
    for (std::size_t p = 1; p < N; ++p)
    {
        std::size_t const firstChild = p * fanout + 1;
        if (firstChild < N)
        {
            m = m.initial(kStates[p], kStates[firstChild]);
        }
    }

    // Each non-Top State carries kTransPerState Transitions on distinct Events,
    // targeting other (non-Top) States -- some self, some Composite, so dispatch
    // exercises self-transitions and Initial-Substate drilling. Distinct Events
    // per Source keep every (Source, Event) pair unique.
    for (std::size_t i = 1; i < N; ++i)
    {
        for (std::size_t t = 0; t < kTransPerState; ++t)
        {
            std::size_t const tgt = 1 + (i + t) % (N - 1);  // in [1, N-1], never Top
            m = m.on(kStates[i], kEvents[t], kStates[tgt]);
        }
    }

    // A Top-level catch-all on the last Event, so an Event no Leaf handles defers
    // up the whole parent chain to Top (the handler of last resort).
    m = m.on(kStates[0], kEvents[numEvents - 1], kStates[1]);

    // Opt the unused enumerators out of the exhaustiveness check.
    for (std::size_t i = N; i < kStates.size(); ++i)
    {
        m = m.unwired(kStates[i]);
    }

    return m;
}

}  // namespace eta_hsm::probe
