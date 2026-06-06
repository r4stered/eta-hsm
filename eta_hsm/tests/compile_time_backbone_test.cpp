// Compile-time backbone: lift the exhaustive single-step differential to
// `consteval`. Because production dispatch and the reference interpreter are both
// constant-expression evaluable, a machine can be proven to agree with its
// reference oracle for every reachable (State, Event) BEFORE it ever runs -- any
// divergence is a static_assert that fails the build.
//
// Every static_assert here holds for a faithful machine. The complementary
// build-failure check -- a planted divergence that MUST fail to compile, naming
// the offending (State, Event) -- lives in compile_time_backbone_negcompile.cpp.

#include <gtest/gtest.h>

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/examples/nested/nested.hpp"
#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/reference/compile_time_backbone.hpp"
#include "eta_hsm/reference/reference_interpreter.hpp"

namespace eta_hsm::reference {
namespace {

using examples::cd_player::Event;
using examples::cd_player::player;
using examples::cd_player::State;

// A single production dispatch run entirely at compile time. From the initial Leaf
// (Stopped), Play settles the machine in Playing. If production dispatch were not
// constant-expression evaluable this would not compile at all.
consteval State playFromStopped()
{
    Machine<player> m;
    m.dispatch(Event::Play);
    return m.identify();
}
static_assert(playFromStopped() == State::Playing);

// The reference interpreter is constant-expression evaluable too: its single-step
// plan for (Stopped, Play) is computed at compile time and settles in Playing,
// matching production. Both oracles now meet at compile time.
consteval State referenceLeafFor(State start, Event e)
{
    auto const view = makeTableView<player>();
    auto const plan = referenceStep(view, start, e, examples::cd_player::Player{});
    return plan.leaf;
}
static_assert(referenceLeafFor(State::Stopped, Event::Play) == State::Playing);
static_assert(referenceLeafFor(State::Stopped, Event::Play) == playFromStopped());

// The single-step differential, run at compile time: from the initial Leaf
// (Stopped), Play makes production and the reference agree on resting Leaf, the
// Exit/Entry chain, AND the full Action-bearing transcript -- all checked by the
// compiler. The returned CtReport is a fixed-size literal usable as a static_assert
// message, so a divergence would name the offending (State, Event).
consteval CtReport oneStepReport()
{
    auto const view = makeTableView<player>();
    CtReport rep;
    Reached<State, Event> const fromInitial{State::Stopped, {}};
    ctDifferentialStep<player>(view, fromInitial, Event::Play, examples::cd_player::Player{}, Fault::None, rep);
    return rep;
}
static_assert(oneStepReport().ok, oneStepReport());

// The EXHAUSTIVE differential over cd_player's entire reachable (State, Event)
// space, run at compile time. Production dispatch agrees with the
// reference -- resting Leaf, Exit/Entry chain, and full transcript -- for every
// reachable pair, proven by the compiler before the machine ever runs. A divergence
// would be a build failure naming the offending (State, Event).
inline constexpr CtReport kCdPlayer = runCompileTimeDifferential<player>();
static_assert(kCdPlayer.ok, kCdPlayer);
// cd_player has five reachable Leaves; the count is the coverage guarantee.
static_assert(kCdPlayer.reachableLeaves == 5);
static_assert(kCdPlayer.pairsVerified == kCdPlayer.reachableLeaves * kCdPlayer.eventCount);

// The guarded branch: with the drawer jammed on the seeded Host, Hammer takes the
// guarded Transition instead of deferring to Top, so the guarded edge enters the
// reachable space and is verified at compile time too.
consteval CtReport jammedReport()
{
    examples::cd_player::Player jammed{};
    jammed.drawer_stuck = true;
    return runCompileTimeDifferential<player>(jammed);
}
static_assert(jammedReport().ok, jammedReport());

// The deep hierarchy holds at compile time too: the exhaustive differential over
// the nested machine -- cross-level Transitions, LCA, Local vs External, Top-sourced
// re-entry, and the full Entry/Exit transcript -- agrees across its whole reachable
// space, proven by the compiler. Nested is no heavier to check than the flat machine.
inline constexpr CtReport kNested = runCompileTimeDifferential<examples::nested::model>();
static_assert(kNested.ok, kNested);
static_assert(kNested.pairsVerified == kNested.reachableLeaves * kNested.eventCount);
static_assert(kNested.reachableLeaves > 0);

// A trivial runtime anchor so the TU is a GTest executable like its siblings; the
// real guarantee is the static_asserts above, checked by the compiler. Echo the
// proven coverage counts so a human reading the test run sees the numbers.
TEST(CompileTimeBackbone, ExhaustiveDifferentialPassesAtCompileTime)
{
    EXPECT_EQ(kCdPlayer.reachableLeaves, 5u);
    EXPECT_EQ(kCdPlayer.pairsVerified, kCdPlayer.reachableLeaves * kCdPlayer.eventCount);
    EXPECT_GT(kNested.reachableLeaves, 0u);
    EXPECT_EQ(kNested.pairsVerified, kNested.reachableLeaves * kNested.eventCount);
}

}  // namespace
}  // namespace eta_hsm::reference
