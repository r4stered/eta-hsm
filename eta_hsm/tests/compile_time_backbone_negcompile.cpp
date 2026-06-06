// Build-failure harness for the compile-time backbone.
//
// This translation unit is COMPILED ON PURPOSE TO FAIL. The compile-time
// differential proves production dispatch agrees with the reference for every
// reachable (State, Event) of cd_player; here a Fault is injected into the
// reference so the two MUST diverge, and the resulting CtReport's static_assert
// must fail the build with a message naming the offending (State, Event). One CASE
// is selected per compile via -DCTBACKBONE_CASE=<n>; tools/expect_compile_fail.sh
// compiles each case and asserts the build fails with the matching offender text.
// This proves what a green static_assert cannot -- that a real divergence is
// caught at compile time and the diagnostic is legible. The companion
// compile_time_backbone_test.cpp covers the faithful machine, where every
// assertion holds.

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/reference/compile_time_backbone.hpp"

namespace {

using namespace eta_hsm::reference;
using eta_hsm::examples::cd_player::player;

#ifndef CTBACKBONE_CASE
#error "define CTBACKBONE_CASE (1..2) to select a negative case"
#endif

#if CTBACKBONE_CASE == 1
// The reference comes to rest in the wrong Leaf for every matched step, so the very
// first reachable matched pair (Stopped + Play) diverges on the resting Leaf.
inline constexpr CtReport report = runCompileTimeDifferential<player>({}, Fault::WrongLeaf);

#elif CTBACKBONE_CASE == 2
// The reference drops the first Exit of every transition, so the first reachable
// pair with a non-empty Exit chain (Stopped + Play) diverges on the Exit sequence.
inline constexpr CtReport report = runCompileTimeDifferential<player>({}, Fault::DropFirstExit);

#else
#error "CTBACKBONE_CASE must be 1..2"
#endif

// The differential found a divergence, so report.ok is false: this static_assert
// fires, naming the offending (State, Event) in its message.
static_assert(report.ok, report);

}  // namespace

int main() {}
