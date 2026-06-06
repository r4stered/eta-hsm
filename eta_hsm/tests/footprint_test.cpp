// Structural-footprint guards for the data-oriented core. These pin the
// properties the README's Performance section boasts, so they cannot silently
// rot: the machine carries no vtable and no heap-owning state of its own, and a
// Machine<Table> is exactly its Host plus a single resting-State word (modulo
// alignment) -- no per-Transition storage, no hidden pointers.
//
// The claims are checked at compile time with static_assert; the GTest case
// exists so the slice runs green under ctest and Bazel and surfaces the measured
// sizes when read. The cd_player table is the instantiation subject, as the
// public example a reader can cross-check against.

#include <gtest/gtest.h>

#include <type_traits>

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/machine/machine.hpp"

namespace eta_hsm {
namespace {

using examples::cd_player::Player;
using examples::cd_player::player;
using CdMachine = Machine<player>;
using CdState = CdMachine::State;

// --- No vtable -----------------------------------------------------------
// Dispatch is generated at compile time from the constexpr table, not resolved
// through a vtable: a Machine is never polymorphic, so an Event costs no virtual
// indirection.
static_assert(!std::is_polymorphic_v<CdMachine>, "Machine must carry no vtable -- dispatch is not virtual.");

// --- Fixed, compile-time-known footprint ---------------------------------
// A Machine is its Host plus a single State word that records the resting Leaf.
// It contains both (lower bound) and adds nothing beyond them but alignment slack
// (upper bound) -- no heap pointer, no per-Transition storage, no growth with the
// table's size. The slack absorbs the zero-information Observer member (the
// default NullObserver) and any padding.
static_assert(sizeof(CdMachine) >= sizeof(Player) + sizeof(CdState),
              "Machine must contain the Host and the resting-State word.");
static_assert(sizeof(CdMachine) <= sizeof(Player) + sizeof(CdState) + alignof(CdMachine),
              "Machine must add no hidden state beyond the Host + State word (modulo alignment).");

// --- No heap-owning state of the machine's own ---------------------------
// A Machine<Table> is only ever as non-trivial as the Host the user supplies:
// the wrapper itself owns nothing that needs destruction. cd_player's Player is
// not trivially destructible (it keeps a std::string event log), so the property
// is shown on a deliberately trivial Host -- a Machine over it is itself trivially
// destructible, proving the machine adds no owning members or heap of its own.
namespace trivial {
struct PlainHost {};
enum class S { Top, Idle };
enum class E { Tick };  // no Transitions reference it; the explicit builder still names the Event enum
inline constexpr auto plain = Hsm<PlainHost, S, E>{}.state(S::Idle, S::Top).initial(S::Top, S::Idle);
}  // namespace trivial

static_assert(std::is_trivially_destructible_v<trivial::PlainHost>);  // sanity: the Host owns nothing
static_assert(std::is_trivially_destructible_v<Machine<trivial::plain>>,
              "Over a trivially-destructible Host the Machine is itself trivially destructible -- it adds no "
              "heap-owning members.");
static_assert(!std::is_polymorphic_v<Machine<trivial::plain>>);

// The static_asserts above do the work at compile time. This case lets the slice
// report green under the test runners and prints the measured sizes when run with
// --gtest_print_time / on failure.
TEST(Footprint, StructuralClaimsHold)
{
    EXPECT_LE(sizeof(CdMachine), sizeof(Player) + sizeof(CdState) + alignof(CdMachine));
    EXPECT_GE(sizeof(CdMachine), sizeof(Player) + sizeof(CdState));
    EXPECT_FALSE(std::is_polymorphic_v<CdMachine>);
}

}  // namespace
}  // namespace eta_hsm
