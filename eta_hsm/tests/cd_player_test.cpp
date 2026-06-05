// Behavioral tests for the cd_player example on the v2 data-oriented core
// (issue 0002). Tests drive the machine through its public interface --
// dispatch / identify / isInSubstateOf -- and assert observable State sequences
// and effects, never internal table layout or generated-dispatch structure.

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/machine/machine.hpp"

#include <gtest/gtest.h>

namespace eta_hsm::examples::cd_player {
namespace {

// The machine forwards Top into its Initial Substate and comes to rest there.
TEST(CdPlayer, RestsInInitialSubstate)
{
    Machine<player> m;
    EXPECT_EQ(m.identify(), State::Stopped);
}

// Dispatching an Event with a matching Transition moves to the Target State.
TEST(CdPlayer, DispatchTakesMatchingTransition)
{
    Machine<player> m;
    m.dispatch(Event::Play);
    EXPECT_EQ(m.identify(), State::Playing);
}

}  // namespace
}  // namespace eta_hsm::examples::cd_player
