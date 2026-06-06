// Behavioral tests for the example_control example: a deep-hierarchy parity
// anchor whose Leaf States nest two levels under Top. Tests drive the machine
// through its public interface -- dispatch / identify / isInSubstateOf / during --
// and assert observable State sequences and the exact Exit/Action/Entry chain,
// never internal table layout. The deep tree lets these assertions cover a
// cross-level Transition that runs through an intermediate Composite ancestor,
// which a single-level machine cannot exercise.

#include "eta_hsm/examples/example_control/example_control.hpp"

#include <gtest/gtest.h>

#include "eta_hsm/machine/machine.hpp"

namespace eta_hsm::examples::example_control {
namespace {

// Construction drills the Initial Substate chain through two nested Composite
// States (Top -> Awake -> Sober) and comes to rest in the Leaf Sober.
TEST(ExampleControl, RestsInInitialSubstate)
{
    Machine<drinker> m;
    EXPECT_EQ(m.identify(), State::Sober);
}

// Construction enters the initial configuration top-down: Entry runs for Top,
// then Awake, then Sober, in order, before any Event is dispatched. This is the
// Composite -> Initial-Substate forwarding, nested twice.
TEST(ExampleControl, ConstructionFiresInitialEntryChain)
{
    Machine<drinker> m;
    EXPECT_EQ(m.host().log, "+Top;+Awake;+Sober;");
    EXPECT_TRUE(m.host().awake);  // Awake's Entry ran and set the flag
}

// A drink the current Leaf does not tip-over on defers to Awake, which raises the
// BAC without changing State: an Internal Transition runs its Action only, no Exit
// or Entry. One beer leaves the machine resting in Sober with a higher BAC.
TEST(ExampleControl, DrinkRaisesBacWithoutLeavingSober)
{
    Machine<drinker> m;
    m.host().log.clear();

    m.dispatch(Event::DrinkBeer);

    EXPECT_EQ(m.identify(), State::Sober);  // unchanged -- handled by Awake's Internal
    EXPECT_FLOAT_EQ(m.host().bac, kBeer);  // BAC rose
    EXPECT_EQ(m.host().log, "beer;");  // Action only -- no Exit/Entry
}

// Drinking accumulates BAC across drinks while Sober, and the drink that would
// carry the BAC to the intoxication threshold tips into Drunk: the Guard on
// Sober's DrinkWhiskey passes, so it takes the Transition instead of deferring.
TEST(ExampleControl, EnoughDrinkTipsSoberIntoDrunk)
{
    Machine<drinker> m;
    m.dispatch(Event::DrinkWhiskey);  // BAC 0 -> kWhiskey; still below threshold
    EXPECT_EQ(m.identify(), State::Sober);

    m.host().log.clear();
    m.dispatch(Event::DrinkWhiskey);  // would cross the threshold: tip into Drunk
    EXPECT_EQ(m.identify(), State::Drunk);
    // The tipping Transition stays within Awake (LCA), so Awake is not exited:
    // only Sober's Exit, the Action, and Drunk's Entry run.
    EXPECT_EQ(m.host().log, "-Sober;whiskey;+Drunk;");
}

// Repeated beers also tip into Drunk, just after more drinks (a beer adds less
// than a whiskey). This exercises Sober's Guarded beer Transition specifically.
TEST(ExampleControl, BeerAccumulatesAndEventuallyTips)
{
    Machine<drinker> m;
    m.dispatch(Event::DrinkBeer);
    m.dispatch(Event::DrinkBeer);
    m.dispatch(Event::DrinkBeer);
    EXPECT_EQ(m.identify(), State::Sober);  // three beers: still below the threshold

    m.dispatch(Event::DrinkBeer);  // the fourth crosses it
    EXPECT_EQ(m.identify(), State::Drunk);
}

// Sober looks at the watch and gets Bored: a sibling Transition inside Awake.
TEST(ExampleControl, SoberLooksAtWatchAndGetsBored)
{
    Machine<drinker> m;
    m.host().log.clear();

    m.dispatch(Event::LookAtWatch);

    EXPECT_EQ(m.identify(), State::Bored);
    EXPECT_EQ(m.host().log, "-Sober;+Bored;");  // within Awake; Awake not exited
}

// Drunk overrides LookAtWatch: where Sober would get Bored, Drunk keeps partying.
// An Internal Transition runs its Action with no State change and no Exit/Entry.
TEST(ExampleControl, DrunkKeepsPartyingOnLookAtWatch)
{
    Machine<drinker> m;
    m.dispatch(Event::DrinkWhiskey);
    m.dispatch(Event::DrinkWhiskey);  // -> Drunk
    ASSERT_EQ(m.identify(), State::Drunk);
    m.host().log.clear();

    m.dispatch(Event::LookAtWatch);

    EXPECT_EQ(m.identify(), State::Drunk);  // unchanged
    EXPECT_EQ(m.host().log, "keep_partying;");  // Action only
}

// Passing out is a cross-level Transition handled by the ancestor Awake: from a
// deep Leaf it exits the Leaf AND Awake, runs the Action, and enters Unconscious
// (the other child of Top). This is the least-common-ancestor chain a one-level
// machine cannot reach.
TEST(ExampleControl, PassOutExitsThroughAncestorToUnconscious)
{
    Machine<drinker> m;
    ASSERT_EQ(m.identify(), State::Sober);
    m.host().log.clear();

    m.dispatch(Event::PassOut);

    EXPECT_EQ(m.identify(), State::Unconscious);
    EXPECT_EQ(m.host().log, "-Sober;-Awake;passout;+Unconscious;");
    EXPECT_FALSE(m.host().awake);  // Awake's Exit ran and cleared the flag
}

// An Event the current Leaf does not handle defers up the parent chain to the
// nearest ancestor that does. Bored declares no handlers of its own, so PassOut
// fires from Awake exactly as it would from Sober.
TEST(ExampleControl, EventDefersUpParentChainFromBored)
{
    Machine<drinker> m;
    m.dispatch(Event::LookAtWatch);  // Sober -> Bored
    ASSERT_EQ(m.identify(), State::Bored);
    m.host().log.clear();

    m.dispatch(Event::PassOut);  // Bored has no handler; Awake handles it

    EXPECT_EQ(m.identify(), State::Unconscious);
    EXPECT_EQ(m.host().log, "-Bored;-Awake;passout;+Unconscious;");
}

// Drinking while Bored also defers to Awake's BAC-only handler: Bored, unlike
// Sober, has no tip-into-Drunk Transition, so it just raises the BAC and stays put
// no matter how high the BAC climbs.
TEST(ExampleControl, DrinkingWhileBoredNeverTipsIntoDrunk)
{
    Machine<drinker> m;
    m.dispatch(Event::LookAtWatch);  // -> Bored
    ASSERT_EQ(m.identify(), State::Bored);

    for (int i = 0; i < 5; ++i)
    {
        m.dispatch(Event::DrinkWhiskey);
    }

    EXPECT_EQ(m.identify(), State::Bored);  // never tips: Bored has no Guarded row
    EXPECT_GT(m.host().bac, kDrunkThreshold);  // and the BAC is well past drunk
}

// An Event no State in the chain handles is ignored: the machine stays put.
// LookAtWatch from Bored defers Bored -> Awake -> Top, none of which handle it.
TEST(ExampleControl, UnhandledEventLeavesStateUnchanged)
{
    Machine<drinker> m;
    m.dispatch(Event::LookAtWatch);  // -> Bored
    ASSERT_EQ(m.identify(), State::Bored);
    m.host().log.clear();

    m.dispatch(Event::LookAtWatch);  // Bored/Awake/Top all decline it

    EXPECT_EQ(m.identify(), State::Bored);  // unchanged
    EXPECT_EQ(m.host().log, "");  // nothing ran
}

// The During tick metabolizes alcohol for the current Leaf without firing a
// Transition: the BAC drops and the machine stays where it is.
TEST(ExampleControl, DuringTickMetabolizesWithoutTransition)
{
    Machine<drinker> m;
    m.dispatch(Event::DrinkBeer);  // raise BAC so the metabolism is observable
    float const before = m.host().bac;
    m.host().log.clear();

    m.during();

    EXPECT_EQ(m.identify(), State::Sober);  // no Transition
    EXPECT_LT(m.host().bac, before);  // metabolized
    EXPECT_EQ(m.host().log, "");  // a During tick fires no Exit/Entry
}

// The During tick follows the current Leaf: in Drunk it runs Drunk's hook, not
// Sober's. Drunk also metabolizes, so the BAC drops while the machine stays Drunk.
TEST(ExampleControl, DuringTickRunsCurrentLeafHookInDrunk)
{
    Machine<drinker> m;
    m.dispatch(Event::DrinkWhiskey);
    m.dispatch(Event::DrinkWhiskey);  // -> Drunk
    ASSERT_EQ(m.identify(), State::Drunk);
    float const before = m.host().bac;
    m.host().log.clear();

    m.during();

    EXPECT_EQ(m.identify(), State::Drunk);  // no Transition
    EXPECT_LT(m.host().bac, before);  // Drunk's During hook metabolized
    EXPECT_EQ(m.host().log, "");
}

// isInSubstateOf reports the current Leaf and all its ancestors. The deep tree
// makes the two-level ancestry observable: Sober is a substate of Awake and Top,
// while Unconscious (a child of Top) is not under Awake.
TEST(ExampleControl, IsInSubstateOfReportsDeepAncestry)
{
    Machine<drinker> m;
    ASSERT_EQ(m.identify(), State::Sober);
    EXPECT_TRUE(m.isInSubstateOf(State::Sober));  // the current Leaf itself
    EXPECT_TRUE(m.isInSubstateOf(State::Awake));  // intermediate Composite ancestor
    EXPECT_TRUE(m.isInSubstateOf(State::Top));  // the root
    EXPECT_FALSE(m.isInSubstateOf(State::Drunk));  // a sibling Leaf

    m.dispatch(Event::PassOut);  // -> Unconscious, the other child of Top
    ASSERT_EQ(m.identify(), State::Unconscious);
    EXPECT_TRUE(m.isInSubstateOf(State::Top));
    EXPECT_FALSE(m.isInSubstateOf(State::Awake));  // Unconscious is not under Awake
}

// A scripted Event run produces the correct ordered sequence of current Leaves:
// drive the machine as a user would and assert the resting State after every
// Dispatch, through accumulation, a tip-over, an override, and a passout.
TEST(ExampleControl, ScriptedNightOutProducesExpectedStateSequence)
{
    Machine<drinker> m;
    ASSERT_EQ(m.identify(), State::Sober);

    struct Step {
        Event event;
        State expected;
    };
    constexpr Step script[] = {
        {Event::DrinkWhiskey, State::Sober},  // below threshold: stays Sober
        {Event::DrinkWhiskey, State::Drunk},  // crosses threshold: tips into Drunk
        {Event::LookAtWatch, State::Drunk},  // Drunk keeps partying (Internal)
        {Event::DrinkBeer, State::Drunk},  // Awake raises BAC; stays Drunk
        {Event::PassOut, State::Unconscious},  // cross-level to Unconscious
        {Event::DrinkBeer, State::Unconscious},  // Unconscious declines everything
    };

    for (auto const& step : script)
    {
        m.dispatch(step.event);
        EXPECT_EQ(m.identify(), step.expected);
    }
}

}  // namespace
}  // namespace eta_hsm::examples::example_control
