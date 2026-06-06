// Behavioral tests for the cd_player example.
// Tests drive the machine through its public interface --
// dispatch / identify / isInSubstateOf -- and assert observable State sequences
// and effects, never internal table layout or generated-dispatch structure.

#include "eta_hsm/examples/cd_player/cd_player.hpp"

#include <gtest/gtest.h>

#include "eta_hsm/machine/machine.hpp"

namespace eta_hsm::examples::cd_player {
namespace {

// The machine forwards Top into its Initial Substate and comes to rest there.
TEST(CdPlayer, RestsInInitialSubstate)
{
    Machine<player> m;
    EXPECT_EQ(m.identify(), State::Stopped);
}

// Construction enters the initial configuration: Entry runs for Top, then for
// the Initial Substate, in order, before any Event is dispatched.
TEST(CdPlayer, ConstructionFiresInitialEntryChain)
{
    Machine<player> m;
    EXPECT_EQ(m.host().log, "+Top;+Stopped;");
    EXPECT_EQ(m.identify(), State::Stopped);
}

// Dispatching an Event with a matching Transition moves to the Target State.
TEST(CdPlayer, DispatchTakesMatchingTransition)
{
    Machine<player> m;
    m.dispatch(Event::Play);
    EXPECT_EQ(m.identify(), State::Playing);
}

// A scripted Event run produces the correct ordered sequence of current States.
// This is the acceptance centerpiece: drive the machine the way a user would and
// assert the Leaf State after every Dispatch.
TEST(CdPlayer, ScriptedRunProducesExpectedStateSequence)
{
    Machine<player> m;
    ASSERT_EQ(m.identify(), State::Stopped);

    struct Step {
        Event event;
        State expected;
    };
    constexpr Step script[] = {
        {Event::Play, State::Playing},  // Stopped -> Playing
        {Event::Pause, State::Paused},  // Playing -> Paused
        {Event::EndPause, State::Playing},  // Paused  -> Playing
        {Event::Stop, State::Stopped},  // Playing -> Stopped
        {Event::OpenClose, State::Open},  // Stopped -> Open
        {Event::OpenClose, State::Empty},  // Open    -> Empty
        {Event::CdDetected, State::Stopped},  // Empty   -> Stopped
    };

    for (auto const& step : script)
    {
        m.dispatch(step.event);
        EXPECT_EQ(m.identify(), step.expected);
    }
}

// An Event with no matching Transition (on the current State or its parent) is
// ignored: the machine stays where it is.
TEST(CdPlayer, UnhandledEventLeavesStateUnchanged)
{
    Machine<player> m;
    ASSERT_EQ(m.identify(), State::Stopped);
    m.dispatch(Event::Stop);  // Stopped handles neither Stop nor defers it anywhere
    EXPECT_EQ(m.identify(), State::Stopped);
    m.dispatch(Event::EndPause);
    EXPECT_EQ(m.identify(), State::Stopped);
}

// An Event a Leaf does not handle defers to its parent. Hammer is declared only
// on Top, so it fires from any Leaf and moves the machine to Playing.
TEST(CdPlayer, EventUnhandledByLeafDefersToParent)
{
    Machine<player> m;
    ASSERT_EQ(m.identify(), State::Stopped);
    m.dispatch(Event::Hammer);  // Stopped has no Hammer; Top handles it
    EXPECT_EQ(m.identify(), State::Playing);

    m.dispatch(Event::Pause);
    ASSERT_EQ(m.identify(), State::Paused);
    m.dispatch(Event::Hammer);  // from a different Leaf, same parent handler
    EXPECT_EQ(m.identify(), State::Playing);
}

// isInSubstateOf asks whether a State is the current Leaf or one of its
// ancestors. Every Leaf is a substate of Top.
TEST(CdPlayer, IsInSubstateOfReportsAncestry)
{
    Machine<player> m;
    ASSERT_EQ(m.identify(), State::Stopped);
    EXPECT_TRUE(m.isInSubstateOf(State::Top));  // Top is the ancestor of all Leaves
    EXPECT_TRUE(m.isInSubstateOf(State::Stopped));  // the current Leaf itself
    EXPECT_FALSE(m.isInSubstateOf(State::Playing));

    m.dispatch(Event::Play);
    ASSERT_EQ(m.identify(), State::Playing);
    EXPECT_TRUE(m.isInSubstateOf(State::Top));
    EXPECT_TRUE(m.isInSubstateOf(State::Playing));
    EXPECT_FALSE(m.isInSubstateOf(State::Stopped));
}

// A Transition runs its Action on the Host exactly once when taken. (Ordering
// relative to Entry/Exit is covered separately; here we only pin the Action.)
TEST(CdPlayer, TransitionRunsItsAction)
{
    Machine<player> m;
    m.dispatch(Event::Play);  // Stopped -> Playing, action start_playback
    EXPECT_NE(m.host().log.find("start_playback;"), std::string::npos);

    m.dispatch(Event::Pause);  // Playing -> Paused, action pause_playback
    EXPECT_NE(m.host().log.find("pause_playback;"), std::string::npos);
}

// A Transition runs, in order: Exit of the State left, the Action, then Entry of
// the State entered. Entry/Exit hooks are auto-detected by reflection.
TEST(CdPlayer, RunsExitThenActionThenEntry)
{
    Machine<player> m;
    m.host().log.clear();  // drop the construction entry chain; focus on the Dispatch
    m.dispatch(Event::Play);  // Stopped -> Playing
    EXPECT_EQ(m.host().log, "-Stopped;start_playback;+Playing;");
}

// A State with no entry/exit hook contributes nothing: reflection calls only the
// hooks the Host actually declares. Open has neither, so leaving Playing for Open
// fires Exit of Playing and the Action, but no Entry.
TEST(CdPlayer, MissingHookIsSilentlySkipped)
{
    Machine<player> m;
    m.dispatch(Event::Play);  // Stopped -> Playing
    m.host().log.clear();
    m.dispatch(Event::OpenClose);  // Playing -> Open (no entry_Open hook)
    EXPECT_EQ(m.host().log, "-Playing;stop_and_open;");
}

// A Guarded Transition whose Guard returns false is not taken: the Event keeps
// deferring up the parent chain. Hammer on Stopped is guarded by drawer_jammed
// (false by default), so it falls through to Top, which handles Hammer -> Playing.
TEST(CdPlayer, GuardFalseDefersToParent)
{
    Machine<player> m;
    ASSERT_EQ(m.identify(), State::Stopped);
    ASSERT_FALSE(m.host().drawer_stuck);  // guard is false
    m.dispatch(Event::Hammer);
    EXPECT_EQ(m.identify(), State::Playing);  // taken by Top, not by Stopped's guarded row
}

// A Top-handled External Transition Exits and re-enters Top, the same way an
// External Transition Exits and re-enters any other Composite Source it targets a
// descendant of. Hammer defers from Stopped to Top, whose row targets Playing, so
// the chain Exits Stopped then Top, runs no Action, and Enters Top then Playing.
TEST(CdPlayer, TopHandledTransitionReentersTop)
{
    Machine<player> m;
    m.host().log.clear();  // drop the construction entry chain
    m.dispatch(Event::Hammer);  // Stopped defers to Top -> Playing
    EXPECT_EQ(m.host().log, "-Stopped;-Top;+Top;+Playing;");
}

// A Guarded Transition whose Guard returns true is taken, in preference to the
// parent's handler for the same Event. With drawer_stuck set, Hammer on Stopped
// runs open_drawer and moves to Open instead of deferring to Top.
TEST(CdPlayer, GuardTrueTakesTheTransition)
{
    Machine<player> m;
    m.host().drawer_stuck = true;  // guard is true
    m.host().log.clear();
    m.dispatch(Event::Hammer);
    EXPECT_EQ(m.identify(), State::Open);
    EXPECT_EQ(m.host().log, "-Stopped;open_drawer;");  // Open has no entry hook
}

// An Internal Transition runs its Action without changing the current State and
// without firing any Exit or Entry. VolumeUp while Playing bumps the volume and
// leaves the machine resting in Playing, no -Playing;/+Playing; in between.
TEST(CdPlayer, InternalTransitionRunsActionWithoutStateChange)
{
    Machine<player> m;
    m.dispatch(Event::Play);  // -> Playing
    m.host().log.clear();
    int const before = m.host().volume;

    m.dispatch(Event::VolumeUp);

    EXPECT_EQ(m.identify(), State::Playing);  // unchanged
    EXPECT_EQ(m.host().volume, before + 1);  // Action ran
    EXPECT_EQ(m.host().log, "turn_up;");  // Action only -- no Exit/Entry
}

// A Self-Transition re-enters the same State: unlike an Internal Transition it
// fires the State's Exit and Entry around the Action. Next while Playing leaves
// the machine in Playing, having run -Playing;next_track;+Playing;.
TEST(CdPlayer, SelfTransitionReentersSameState)
{
    Machine<player> m;
    m.dispatch(Event::Play);  // -> Playing
    m.host().log.clear();

    m.dispatch(Event::Next);

    EXPECT_EQ(m.identify(), State::Playing);  // back in the same State
    EXPECT_EQ(m.host().log, "-Playing;next_track;+Playing;");
}

}  // namespace
}  // namespace eta_hsm::examples::cd_player
