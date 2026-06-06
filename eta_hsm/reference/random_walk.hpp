#pragma once

// The random-walk layer on top of the exhaustive BFS backbone. It drives a single
// live Machine through a seeded random stream of Events -- with During ticks
// interleaved -- and compares production dispatch against the reference interpreter
// step by step along the whole walk. Because one Machine walks forward (Host state
// accumulating), the walk reaches sequence-dependent configurations the single-step
// backbone cannot: guards gated on accumulated Host state, and During ticks that
// mutate that state between Events. The reference reads the live Host before each
// Event, so its Guard outcomes track production's as the walk evolves.
//
// When a walk diverges, the hand-rolled bisect shrinker (shrink) minimizes it to
// the smallest sub-walk that still reproduces the divergence -- drop-half first,
// then single-step removal -- so a long failing walk collapses to a short repro.
// The seed alone reproduces a run (the PRNG is a pure function of the seed).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "eta_hsm/reference/backbone.hpp"

namespace eta_hsm::reference {

// A deterministic PRNG (splitmix64), so a seed reproduces a walk identically and
// portably -- no dependence on a standard-library distribution's implementation.
struct Rng {
    std::uint64_t state{0};

    std::uint64_t next()
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // A value in [0, n); 0 when n == 0.
    std::size_t below(std::size_t n)
    {
        if (n == 0)
        {
            return 0;
        }
        return next() % n;
    }
};

// What a single step of a walk does: deliver an Event, run a During tick, or run an
// input-consuming During tick. During ticks must be inert (no Transition).
enum class WalkKind { Event, During, DuringInput };

template <class EventEnum>
struct WalkStep {
    WalkKind kind{WalkKind::Event};
    EventEnum event{};  // meaningful when kind == Event
    int input{0};  // forwarded to during(input) when kind == DuringInput

    bool operator==(const WalkStep&) const = default;
};

// Generate a seeded random walk: `length` steps, each an Event (more common) or a
// During / input-During tick interleaved between them. The seed alone reproduces
// the stream. Events are drawn uniformly from the Event enum.
template <class EventEnum>
std::vector<WalkStep<EventEnum>> generateWalk(std::uint64_t seed, std::size_t length)
{
    constexpr auto events = eta_hsm::enum_values<EventEnum>();
    Rng rng{seed};
    std::vector<WalkStep<EventEnum>> walk;
    walk.reserve(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        WalkStep<EventEnum> step;
        std::size_t const roll = rng.below(5);  // 3/5 Event, 1/5 During, 1/5 input-During
        if (roll < 3)
        {
            step.kind = WalkKind::Event;
            step.event = events[rng.below(events.size())];
        }
        else if (roll == 3)
        {
            step.kind = WalkKind::During;
        }
        else
        {
            step.kind = WalkKind::DuringInput;
            step.input = static_cast<int>(rng.below(10));
        }
        walk.push_back(step);
    }
    return walk;
}

// The outcome of replaying a walk against the differential: where it first diverged
// (if anywhere), how many steps were verified before that, and the resting Leaf
// after each verified step (parallel to the walk indices). `detail` names the
// diverging step and the facet that disagreed, for the repro report.
template <class State, class EventEnum>
struct WalkResult {
    bool diverged{false};
    std::size_t stepIndex{0};  // index into the walk of the diverging step
    std::size_t stepsVerified{0};
    std::size_t duringTicks{0};  // During / input-During ticks executed
    std::vector<State> visited;  // resting Leaf after each verified step
    State refLeaf{};
    State prodLeaf{};
    std::string detail;
};

// The faithful oracle: the reference single-step, unmodified. The default for
// runWalk; a test supplies a deliberately wrong oracle for fault injection.
struct FaithfulOracle {
    template <class State, class EventEnum, class Host>
    Plan<State, Host> operator()(const TableView<State, EventEnum, Host>& view, State start, EventEnum event,
                                 const Host& host) const
    {
        return referenceStep(view, start, event, host);
    }
};

// Drive a single live Machine through `walk`, asserting production agrees with the
// reference at every Event step (resting Leaf, Exit/Entry chain, and -- for a
// log-bearing Host -- the per-step transcript) and that every During tick is inert.
// The reference reads the live Host before each Event, so Guard outcomes track
// production's as accumulated state evolves. Returns at the first divergence.
template <auto Table, class State, class EventEnum, class Host, class Oracle = FaithfulOracle>
WalkResult<State, EventEnum> runWalk(const TableView<State, EventEnum, Host>& view,
                                     const std::vector<WalkStep<EventEnum>>& walk, Oracle oracle = {})
{
    using Obs = RecordingObserver<State>;
    WalkResult<State, EventEnum> res;

    std::vector<State> exits;
    std::vector<State> entries;
    Machine<Table, Obs> m{Obs{&exits, &entries}};
    if constexpr (HasLog<Host>)
    {
        m.host().log.clear();  // start the per-step transcript accounting fresh
    }

    State leaf = m.identify();
    for (std::size_t i = 0; i < walk.size(); ++i)
    {
        WalkStep<EventEnum> const& step = walk[i];
        if (step.kind == WalkKind::Event)
        {
            Host const hostBefore = m.host();  // guards read the pre-Action Host, as production does
            auto const plan = oracle(view, leaf, step.event, hostBefore);
            State const expLeaf = plan.matched ? plan.leaf : leaf;

            std::string logBefore;
            if constexpr (HasLog<Host>)
            {
                logBefore = m.host().log;
            }
            exits.clear();
            entries.clear();
            m.dispatch(step.event);
            State const prodLeaf = m.identify();

            std::string detail;
            if (prodLeaf != expLeaf)
            {
                detail = "resting Leaf mismatch";
            }
            else if (exits != plan.exits)
            {
                detail = "Exit sequence mismatch";
            }
            else if (entries != plan.entries)
            {
                detail = "Entry sequence mismatch";
            }
            else if constexpr (HasLog<Host>)
            {
                std::string const delta = m.host().log.substr(logBefore.size());
                if (delta != renderOn(plan).log)
                {
                    detail = "transcript mismatch";
                }
            }
            if (!detail.empty())
            {
                res.diverged = true;
                res.stepIndex = i;
                res.refLeaf = expLeaf;
                res.prodLeaf = prodLeaf;
                res.detail = detail + " at step " + std::to_string(i) + " (" + stepLabel(leaf, step.event) + ")";
                return res;
            }
            leaf = prodLeaf;
        }
        else
        {
            // A During tick must be inert: no Leaf change and no Exit/Entry.
            State const before = m.identify();
            exits.clear();
            entries.clear();
            if (step.kind == WalkKind::During)
            {
                m.during();
            }
            else
            {
                m.during(step.input);
            }
            if (m.identify() != before || !exits.empty() || !entries.empty())
            {
                res.diverged = true;
                res.stepIndex = i;
                res.refLeaf = before;
                res.prodLeaf = m.identify();
                res.detail = "During tick was not inert at step " + std::to_string(i);
                return res;
            }
            leaf = m.identify();
            ++res.duringTicks;
        }
        res.visited.push_back(leaf);
        ++res.stepsVerified;
    }
    return res;
}

// The hand-rolled bisect shrinker. Given a walk that reproduces a divergence
// (`stillReproduces(walk)` is true) and a predicate that replays a candidate sub-walk
// and reports whether it still reproduces, minimize the walk to a small sub-walk that
// still reproduces -- drop a whole half when that still reproduces, otherwise drop the
// first single step whose removal preserves it, repeating until nothing more can go.
// The result is 1-minimal: removing any single remaining step stops reproducing.
template <class EventEnum, class Pred>
std::vector<WalkStep<EventEnum>> shrink(std::vector<WalkStep<EventEnum>> walk, Pred stillReproduces)
{
    bool progress = true;
    while (progress)
    {
        progress = false;

        // Drop-half: discarding a whole half at once is the fast path on a long walk.
        if (walk.size() > 1)
        {
            std::size_t const mid = walk.size() / 2;
            std::vector<WalkStep<EventEnum>> first(walk.begin(),
                                                   walk.begin() + static_cast<std::ptrdiff_t>(mid));
            std::vector<WalkStep<EventEnum>> second(walk.begin() + static_cast<std::ptrdiff_t>(mid), walk.end());
            if (stillReproduces(first))
            {
                walk = std::move(first);
                progress = true;
                continue;
            }
            if (stillReproduces(second))
            {
                walk = std::move(second);
                progress = true;
                continue;
            }
        }

        // Single-step removal: drop the first step whose removal still reproduces.
        // Repeated to a fixpoint, this drives the walk to a 1-minimal repro.
        for (std::size_t i = 0; i < walk.size(); ++i)
        {
            std::vector<WalkStep<EventEnum>> cand = walk;
            cand.erase(cand.begin() + static_cast<std::ptrdiff_t>(i));
            if (stillReproduces(cand))
            {
                walk = std::move(cand);
                progress = true;
                break;
            }
        }
    }
    return walk;
}

}  // namespace eta_hsm::reference
