// Differential tests for the reference interpreter (the executable oracle) over
// the flat cd_player machine. The reference walks a runtime view of the table
// directly; the production Machine is asserted to agree with it -- resting Leaf
// and Exit/Action/Entry transcript -- across the machine's reachable space, with
// no hand-authored expected transcript strings.

#include "eta_hsm/reference/reference_interpreter.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/machine/machine.hpp"

namespace eta_hsm::reference {
namespace {

using examples::cd_player::Event;
using examples::cd_player::Player;
using examples::cd_player::player;
using examples::cd_player::State;

// Convenience: compute a step's plan from the default (un-jammed) Host and render
// its transcript onto a fresh Host, returning {leaf, transcript}.
struct Outcome {
    State leaf;
    std::string transcript;
};

Outcome interpret(State start, Event event, bool jammed = false, Fault fault = Fault::None)
{
    static const auto view = makeTableView<player>();
    Player guardHost{};
    guardHost.drawer_stuck = jammed;
    auto const plan = referenceStep(view, start, event, guardHost, fault);
    return {plan.leaf, renderOn(plan).log};
}

// Tracer bullet: an ordinary External Transition. Stopped + Play exits Stopped,
// runs start_playback, enters Playing, and comes to rest there.
TEST(ReferenceInterpreter, ExternalTransitionExitActionEntry)
{
    auto const out = interpret(State::Stopped, Event::Play);
    EXPECT_EQ(out.leaf, State::Playing);
    EXPECT_EQ(out.transcript, "-Stopped;start_playback;+Playing;");
}

// An Internal Transition runs its Action only -- no State change, no Exit/Entry.
TEST(ReferenceInterpreter, InternalTransitionRunsActionOnly)
{
    auto const out = interpret(State::Playing, Event::VolumeUp);
    EXPECT_EQ(out.leaf, State::Playing);
    EXPECT_EQ(out.transcript, "turn_up;");
}

// A Self-Transition re-enters the same State, firing its Exit and Entry around
// the Action.
TEST(ReferenceInterpreter, SelfTransitionReentersState)
{
    auto const out = interpret(State::Playing, Event::Next);
    EXPECT_EQ(out.leaf, State::Playing);
    EXPECT_EQ(out.transcript, "-Playing;next_track;+Playing;");
}

// A Target State with no Entry hook contributes nothing: Open has neither Entry
// nor Exit, so leaving Playing for Open fires Exit of Playing and the Action only.
TEST(ReferenceInterpreter, MissingHookOnTargetIsSkipped)
{
    auto const out = interpret(State::Playing, Event::OpenClose);
    EXPECT_EQ(out.leaf, State::Open);
    EXPECT_EQ(out.transcript, "-Playing;stop_and_open;");
}

// A Guard that is false keeps the Event deferring up the parent chain. Hammer on
// Stopped (guard drawer_jammed false) defers to Top, whose row targets Playing;
// the Top-sourced External Transition Exits and re-enters Top.
TEST(ReferenceInterpreter, GuardFalseDefersToTopAndReentersTop)
{
    auto const out = interpret(State::Stopped, Event::Hammer);
    EXPECT_EQ(out.leaf, State::Playing);
    EXPECT_EQ(out.transcript, "-Stopped;-Top;+Top;+Playing;");
}

// A Guard that is true takes the Transition in preference to the parent handler.
// With the drawer jammed, Hammer on Stopped runs open_drawer and moves to Open.
TEST(ReferenceInterpreter, GuardTrueTakesTheGuardedTransition)
{
    auto const out = interpret(State::Stopped, Event::Hammer, /*jammed=*/true);
    EXPECT_EQ(out.leaf, State::Open);
    EXPECT_EQ(out.transcript, "-Stopped;open_drawer;");
}

// An Event no State handles (on the Leaf or any ancestor) is a strict no-op: the
// Leaf is unchanged and nothing runs.
TEST(ReferenceInterpreter, UnhandledEventIsAStrictNoOp)
{
    auto const out = interpret(State::Stopped, Event::Stop);
    EXPECT_EQ(out.leaf, State::Stopped);
    EXPECT_EQ(out.transcript, "");
}

// The reference doubles as the reachability-graph builder. From the initial Leaf
// it enumerates exactly the States reachable by some Event sequence; Broken, which
// no Transition targets, is unreachable and absent.
TEST(ReferenceInterpreter, ReachableSetExcludesUnreachableStates)
{
    static const auto view = makeTableView<player>();
    EXPECT_EQ(initialLeaf(view), State::Stopped);

    Player guardHost{};
    auto const reached = reachable(view, guardHost);

    std::set<State> leaves;
    for (auto const& r : reached)
    {
        leaves.insert(r.leaf);
    }
    EXPECT_EQ(leaves, (std::set<State>{State::Stopped, State::Open, State::Empty, State::Playing, State::Paused}));
    EXPECT_FALSE(leaves.contains(State::Broken));  // no Transition reaches Broken
}

// Each replay path actually drives the live Machine to the claimed Leaf -- the
// reachability graph is sound, so the differential can trust it to position the
// live Machine.
TEST(ReferenceInterpreter, ReplayPathsDriveLiveMachineToLeaf)
{
    static const auto view = makeTableView<player>();
    Player guardHost{};
    for (auto const& r : reachable(view, guardHost))
    {
        Machine<player> m;
        for (Event e : r.path)
        {
            m.dispatch(e);
        }
        EXPECT_EQ(m.identify(), r.leaf);
    }
}

// A readable "<State>+<Event>" tag for failure messages.
std::string label(State s, Event e)
{
    auto sn = enum_name(s);
    auto en = enum_name(e);
    return std::string{sn ? *sn : "?"} + "+" + std::string{en ? *en : "?"};
}

// The centerpiece: for EVERY reachable (State, Event), drive the live Machine to
// the State (by replaying a path the reference computed) and assert production
// dispatch agrees with the reference on both the resting Leaf and the
// Exit/Action/Entry transcript. No expected transcript string is hand-authored --
// every expectation is computed by the reference from the table.
//
// The compared transcript is the Host's record of the Exit/Action/Entry effects
// -- the observable work dispatch performs. The machine runs with the default
// NullObserver, so the Observer notification seam is out of this comparison's
// scope (the auto-logging suite exercises that seam directly).
TEST(ReferenceInterpreter, ProductionAgreesWithReferenceOverReachableSpace)
{
    static const auto view = makeTableView<player>();
    constexpr auto events = enum_values<Event>();
    Player const guardHost{};

    std::size_t pairs = 0;
    for (auto const& r : reachable(view, guardHost))
    {
        for (Event e : events)
        {
            // Production: drive to the State, isolate this single step's transcript.
            Machine<player> m;
            for (Event pe : r.path)
            {
                m.dispatch(pe);
            }
            ASSERT_EQ(m.identify(), r.leaf) << "replay path landed wrong for " << label(r.leaf, e);
            m.host().log.clear();
            m.dispatch(e);

            // Reference: compute the same single step independently.
            auto const plan = referenceStep(view, r.leaf, e, guardHost);

            EXPECT_EQ(m.identify(), plan.leaf) << "resting Leaf mismatch at " << label(r.leaf, e);
            EXPECT_EQ(m.host().log, renderOn(plan).log) << "transcript mismatch at " << label(r.leaf, e);
            ++pairs;
        }
    }
    EXPECT_EQ(pairs, 5u * events.size());  // 5 reachable Leaves x every Event
}

// Fault injection proves the comparison bites: a deliberately wrong reference step
// must disagree with production on the very facets the differential checks.
TEST(ReferenceInterpreter, DifferentialCatchesAPlantedFault)
{
    static const auto view = makeTableView<player>();
    Player const guardHost{};

    Machine<player> m;
    m.host().log.clear();
    m.dispatch(Event::Play);  // Stopped -> Playing, a step with both an Exit and an Entry
    State const prodLeaf = m.identify();
    std::string const prodLog = m.host().log;

    // The faithful step agrees, so the disagreements below are the fault, not noise.
    auto const good = referenceStep(view, State::Stopped, Event::Play, guardHost, Fault::None);
    ASSERT_EQ(prodLeaf, good.leaf);
    ASSERT_EQ(prodLog, renderOn(good).log);

    // A dropped Exit corrupts the transcript: the differential's transcript check bites.
    auto const droppedExit = referenceStep(view, State::Stopped, Event::Play, guardHost, Fault::DropFirstExit);
    EXPECT_NE(prodLog, renderOn(droppedExit).log);

    // A wrong resting Leaf: the differential's Leaf check bites.
    auto const wrongLeaf = referenceStep(view, State::Stopped, Event::Play, guardHost, Fault::WrongLeaf);
    EXPECT_NE(prodLeaf, wrongLeaf.leaf);
}

// The guarded branch reachability alone does not exercise (no Event sets the
// drawer jammed): with the drawer jammed on BOTH sides, Hammer on Stopped takes
// the guarded Transition to Open instead of deferring to Top, and the two agree.
TEST(ReferenceInterpreter, ProductionAgreesWithReferenceOnGuardedBranch)
{
    static const auto view = makeTableView<player>();
    Player jammed{};
    jammed.drawer_stuck = true;

    Machine<player> m;
    m.host().drawer_stuck = true;
    m.host().log.clear();
    m.dispatch(Event::Hammer);

    auto const plan = referenceStep(view, State::Stopped, Event::Hammer, jammed);
    EXPECT_EQ(m.identify(), plan.leaf);
    EXPECT_EQ(m.host().log, renderOn(plan).log);
    EXPECT_EQ(plan.leaf, State::Open);  // the guarded Transition, not the Top deferral
}

}  // namespace
}  // namespace eta_hsm::reference
