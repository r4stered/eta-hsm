// Tests for the random-walk layer: seeded random Event streams (with During ticks
// interleaved) compared step-by-step against the reference interpreter, plus the
// hand-rolled bisect shrinker that minimizes a diverging walk to its smallest
// reproducing Event sequence. The walk reaches sequence-dependent states the
// single-step backbone cannot -- guards gated on accumulated Host state, and During
// ticks that mutate that state between Events.

#include "eta_hsm/reference/random_walk.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/examples/example_control/example_control.hpp"

namespace eta_hsm::reference {
namespace {

// A planted dispatch bug, expressed as a deliberately wrong oracle: it agrees with
// the faithful reference everywhere EXCEPT the step that comes to rest in Drunk,
// where it reports the wrong resting Leaf. Drunk is reachable only after enough
// accumulated drinking, so this divergence surfaces only along a multi-Event
// sequence -- exactly the deep bug the random walk exists to catch.
struct DrunkBugOracle {
    using State = examples::example_control::State;

    template <class EventEnum, class Host>
    Plan<State, Host> operator()(const TableView<State, EventEnum, Host>& view, State start, EventEnum event,
                                 const Host& host) const
    {
        auto plan = referenceStep(view, start, event, host);
        if (plan.leaf == State::Drunk)
        {
            plan.leaf = State::Sober;  // the bug: a wrong resting Leaf on entry to Drunk
        }
        return plan;
    }
};

// A walk of plain Events (for readable construction and comparison).
std::vector<WalkStep<examples::example_control::Event>> events(
    std::initializer_list<examples::example_control::Event> es)
{
    std::vector<WalkStep<examples::example_control::Event>> walk;
    for (auto e : es)
    {
        walk.push_back({WalkKind::Event, e, 0});
    }
    return walk;
}

// Tracer bullet: a seeded random walk over the flat cd_player agrees with the
// reference at every step, and the seed alone reproduces the walk -- the same seed
// yields an identical step stream and an identical (clean) outcome.
TEST(RandomWalk, SeededWalkOverCdPlayerAgreesWithReference)
{
    auto const view = makeTableView<examples::cd_player::player>();

    auto const walk = generateWalk<examples::cd_player::Event>(/*seed=*/1234u, /*length=*/40);
    auto const result = runWalk<examples::cd_player::player>(view, walk);

    EXPECT_FALSE(result.diverged) << result.detail;
    EXPECT_GT(result.stepsVerified, 0u);

    // The seed alone reproduces the walk: regenerating from the same seed yields an
    // identical step stream, hence an identical outcome.
    auto const walk2 = generateWalk<examples::cd_player::Event>(/*seed=*/1234u, /*length=*/40);
    EXPECT_EQ(walk, walk2);
}

// The meat: a seeded random walk over example_control, with During ticks
// interleaved, agrees with the reference step-by-step AND reaches the
// sequence-dependent Drunk state -- only reachable after enough drinking carries
// the accumulated BAC across the intoxication threshold, which the single-step
// backbone (re-seeding to a pristine Host each step) can never reach. Every During
// tick is inert (else runWalk would have diverged), and at least one tick runs
// while resting in a During-hook-bearing Leaf (Sober/Drunk), so invariant #6 is
// non-vacuous: the tick metabolizes BAC without firing any Transition.
TEST(RandomWalk, ExampleControlWalkReachesDrunkWithDuringInterleaved)
{
    using examples::example_control::State;
    auto const view = makeTableView<examples::example_control::drinker>();

    // Search seeds for a walk that reaches Drunk; every walk examined must be clean.
    bool reachedDrunk = false;
    bool duringHookExercised = false;
    std::size_t totalDuringTicks = 0;
    for (std::uint64_t seed = 0; seed < 256 && !(reachedDrunk && duringHookExercised); ++seed)
    {
        auto const walk = generateWalk<examples::example_control::Event>(seed, /*length=*/60);
        auto const result = runWalk<examples::example_control::drinker>(view, walk);
        ASSERT_FALSE(result.diverged) << "seed " << seed << ": " << result.detail;

        totalDuringTicks += result.duringTicks;
        for (std::size_t i = 0; i < result.visited.size(); ++i)
        {
            if (result.visited[i] == State::Drunk)
            {
                reachedDrunk = true;
            }
            bool const isDuring = walk[i].kind != WalkKind::Event;
            if (isDuring && (result.visited[i] == State::Sober || result.visited[i] == State::Drunk))
            {
                duringHookExercised = true;
            }
        }
    }

    EXPECT_TRUE(reachedDrunk) << "no seeded walk reached the sequence-dependent Drunk state";
    EXPECT_TRUE(duringHookExercised) << "no During tick ran on a During-hook-bearing Leaf";
    EXPECT_GT(totalDuringTicks, 0u);
}

// The bisect shrinker minimizes a diverging walk to its smallest reproducing Event
// sequence. A walk that reaches Drunk via two whiskeys, padded with irrelevant
// During ticks and a watch-glance, shrinks -- drop-half then single-step removal --
// to exactly the two-whiskey repro. The During ticks (which only metabolize BAC,
// delaying the threshold) and the unrelated Event are all stripped away.
TEST(RandomWalk, ShrinkerMinimizesToTwoWhiskeyRepro)
{
    using Event = examples::example_control::Event;
    auto const view = makeTableView<examples::example_control::drinker>();

    // Padded walk: two whiskeys reach Drunk; the During ticks (which only
    // metabolize BAC) and the trailing watch-glance are all noise to be stripped.
    std::vector<WalkStep<Event>> walk{
        {WalkKind::During, {}, 0},
        {WalkKind::Event, Event::DrinkWhiskey, 0},
        {WalkKind::DuringInput, {}, 5},
        {WalkKind::During, {}, 0},
        {WalkKind::Event, Event::DrinkWhiskey, 0},
        {WalkKind::During, {}, 0},
        {WalkKind::Event, Event::LookAtWatch, 0},
    };

    auto const stillReproduces = [&](const std::vector<WalkStep<Event>>& w) {
        return runWalk<examples::example_control::drinker>(view, w, DrunkBugOracle{}).diverged;
    };
    ASSERT_TRUE(stillReproduces(walk));  // the padded walk does reproduce the bug

    auto const minimal = shrink<Event>(walk, stillReproduces);
    EXPECT_EQ(minimal, events({Event::DrinkWhiskey, Event::DrinkWhiskey}));
}

// End-to-end fault injection: the planted dispatch bug is reachable only via a
// multi-Event sequence (enough drinking to reach Drunk). The harness searches seeds
// until a random walk trips it, then the shrinker collapses that ~60-step walk to a
// minimal reproducing Event sequence -- the seed plus that short sequence are the
// repro. The minimal repro is verified to genuinely reach the sequence-dependent
// Drunk state, to be 1-minimal, and to contain no During ticks (all stripped).
TEST(RandomWalk, RandomSearchFindsAndShrinksPlantedBug)
{
    using Event = examples::example_control::Event;
    using State = examples::example_control::State;
    auto const view = makeTableView<examples::example_control::drinker>();

    auto const stillReproduces = [&](const std::vector<WalkStep<Event>>& w) {
        return runWalk<examples::example_control::drinker>(view, w, DrunkBugOracle{}).diverged;
    };

    // The harness hunts: seeded random walks until one triggers the planted bug.
    std::vector<WalkStep<Event>> failing;
    std::uint64_t foundSeed = 0;
    bool found = false;
    for (std::uint64_t seed = 0; seed < 512; ++seed)
    {
        auto walk = generateWalk<Event>(seed, /*length=*/60);
        if (stillReproduces(walk))
        {
            failing = std::move(walk);
            foundSeed = seed;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found) << "no seeded walk in the searched range triggered the planted bug";

    auto const minimal = shrink<Event>(failing, stillReproduces);

    EXPECT_TRUE(stillReproduces(minimal)) << "shrunk repro (seed " << foundSeed << ") no longer reproduces";
    EXPECT_LT(minimal.size(), failing.size());
    EXPECT_GE(minimal.size(), 2u);  // Drunk needs at least two drinks

    // Every remaining step is an Event -- During ticks (which only delay reaching the
    // threshold) are all stripped.
    for (auto const& s : minimal)
    {
        EXPECT_EQ(s.kind, WalkKind::Event);
    }

    // 1-minimal: removing any single remaining step stops reproducing the bug.
    for (std::size_t i = 0; i < minimal.size(); ++i)
    {
        auto cand = minimal;
        cand.erase(cand.begin() + static_cast<std::ptrdiff_t>(i));
        EXPECT_FALSE(stillReproduces(cand)) << "step " << i << " of the minimal repro was not essential";
    }

    // The minimal repro genuinely reaches the sequence-dependent Drunk state under
    // the faithful reference -- it is a real deep-state repro, not an artifact.
    auto const faithful = runWalk<examples::example_control::drinker>(view, minimal);
    EXPECT_FALSE(faithful.diverged);
    bool reachedDrunk = false;
    for (State leaf : faithful.visited)
    {
        if (leaf == State::Drunk)
        {
            reachedDrunk = true;
        }
    }
    EXPECT_TRUE(reachedDrunk) << "minimal repro (seed " << foundSeed << ") does not reach Drunk";
}

// Breadth: many seeds over both the flat cd_player and the deep example_control all
// run clean against the faithful reference -- production agrees step-by-step across
// hundreds of distinct random walks, with During ticks interleaved throughout. A
// real dispatch regression in any reachable deep sequence would surface here.
TEST(RandomWalk, ManySeedsRunCleanOnBothMachines)
{
    auto const cdView = makeTableView<examples::cd_player::player>();
    auto const ecView = makeTableView<examples::example_control::drinker>();

    std::size_t cdSteps = 0;
    std::size_t ecSteps = 0;
    for (std::uint64_t seed = 0; seed < 300; ++seed)
    {
        auto const cdWalk = generateWalk<examples::cd_player::Event>(seed, /*length=*/50);
        auto const cd = runWalk<examples::cd_player::player>(cdView, cdWalk);
        ASSERT_FALSE(cd.diverged) << "cd_player seed " << seed << ": " << cd.detail;
        cdSteps += cd.stepsVerified;

        auto const ecWalk = generateWalk<examples::example_control::Event>(seed, /*length=*/50);
        auto const ec = runWalk<examples::example_control::drinker>(ecView, ecWalk);
        ASSERT_FALSE(ec.diverged) << "example_control seed " << seed << ": " << ec.detail;
        ecSteps += ec.stepsVerified;
    }

    EXPECT_EQ(cdSteps, 300u * 50u);  // every step of every walk verified
    EXPECT_EQ(ecSteps, 300u * 50u);
}

}  // namespace
}  // namespace eta_hsm::reference
