// Behavioral tests for the maximal compile-time validator.
//
// The validator's *logic* is exercised here through its public entry point,
// eta_hsm::validate<Table>(), which returns a ValidationReport without firing any
// static_assert. A well-formed machine reports {ok=true}; each ill-formed machine
// reports {ok=false} with the ValidationError naming the failed check. Because
// validate() is consteval, every case is asserted both at compile time
// (static_assert) and at runtime (EXPECT), so a regression is caught whichever way
// the table is consumed.
//
// The companion build-failure harness (tools/expect_compile_fail.sh, wired as
// ctest cases) proves the other half: that instantiating Machine<bad_table> really
// fails to compile and that the diagnostic names the offending element. That is
// the part a pure-runtime test cannot show.

#include "eta_hsm/machine/validator.hpp"

#include <gtest/gtest.h>

#include "eta_hsm/examples/cd_player/cd_player.hpp"
#include "eta_hsm/examples/nested/nested.hpp"
#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm::validator_test {
namespace {

// A tiny Host and enums the negative tables below are built from. Each bad table
// violates exactly one check and is otherwise well-formed, so the first-failure
// report names the check under test.
struct H {
    bool g1() const { return true; }
    bool g2() const { return false; }
};
enum class S { Top, A, B, C };
enum class E { Go, Stop };

// The real cd_player machine is well-formed: a flat machine with one Top, every
// State parented, the Top Composite forwarding into a declared child, every Target
// declared, every enumerator wired, and no conflicting Transitions.
TEST(Validator, ValidFlatMachinePasses)
{
    constexpr auto report = validate<examples::cd_player::player>();
    static_assert(report.ok);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.error, ValidationError::None);
}

// The nested example is well-formed too: a hierarchy nested two deep, exercising
// every check in the presence of Composite States and cross-level Transitions.
TEST(Validator, ValidHierarchicalMachinePasses)
{
    constexpr auto report = validate<examples::nested::model>();
    static_assert(report.ok);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.error, ValidationError::None);
}

// Check 1: a machine declared with no Top State (only .state calls, never the
// .initial that registers a Top) is rejected.
inline constexpr auto no_top = Hsm<H, S, E>{}.state(S::A, S::Top);
TEST(Validator, NoTopStateRejected)
{
    constexpr auto report = validate<no_top>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::NoTopState);
}

// Check 1: a machine with two Top States is rejected. The second Top row is added
// surgically because the builder offers no way to declare two -- which is the
// point: such a table is only reachable by mistake.
inline constexpr auto two_tops = [] {
    auto t = Hsm<H, S, E>{}.state(S::A, S::Top).initial(S::Top, S::A);
    t.states[t.stateCount++] = StateRow<S>{S::B, S::B, true, false, {}};
    return t;
}();
TEST(Validator, MultipleTopStatesRejected)
{
    constexpr auto report = validate<two_tops>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::MultipleTopStates);
}

// Check 2: a non-Top State whose parent is never declared is rejected. Here B's
// parent C has no row (C is only an enumerator), so B has no place in the tree.
inline constexpr auto missing_parent = Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::C);
TEST(Validator, MissingParentRejected)
{
    constexpr auto report = validate<missing_parent>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::MissingParent);
}

// Check 3: a Composite State (A is the parent of B) with no Initial Substate is
// rejected -- the machine would have nowhere to rest after entering A.
inline constexpr auto composite_no_initial = Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::A);
TEST(Validator, CompositeWithoutInitialRejected)
{
    constexpr auto report = validate<composite_no_initial>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::InitialNotChild);
}

// Check 3: a Composite State whose Initial Substate is not one of its children is
// rejected. A is the parent of B, but its declared initial C lives under Top.
inline constexpr auto initial_not_child =
    Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::A).state(S::C, S::Top).initial(S::A, S::C);
TEST(Validator, InitialSubstateNotAChildRejected)
{
    constexpr auto report = validate<initial_not_child>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::InitialNotChild);
}

// Check 4: a Transition whose Target is not a declared State is rejected. The
// (A, Go) Transition targets C, which has no row.
inline constexpr auto bad_target = Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).on(S::A, E::Go, S::C);
TEST(Validator, TransitionTargetNotDeclaredRejected)
{
    constexpr auto report = validate<bad_target>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::TargetNotDeclared);
}

// Check 5: every enumerator of the State enum must be wired into the tree. Here
// the enum has Top, A, B, C but only Top and A are declared, so B (and C) are
// unwired and the machine is rejected.
inline constexpr auto unwired_enumerator = Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top);
TEST(Validator, UnwiredEnumeratorRejected)
{
    constexpr auto report = validate<unwired_enumerator>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::EnumeratorNotWired);
}

// Opt-out: marking the unwired enumerators .unwired suppresses check 5, so a
// work-in-progress machine with planned-but-unbuilt States validates cleanly.
inline constexpr auto unwired_opted_out =
    Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).unwired(S::B).unwired(S::C);
TEST(Validator, UnwiredMarkerSuppressesExhaustiveness)
{
    constexpr auto report = validate<unwired_opted_out>();
    static_assert(report.ok);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.error, ValidationError::None);
}

// The opt-out is per State: marking only B .unwired still rejects the machine for
// the remaining unwired enumerator C. The marker suppresses one State, not all.
inline constexpr auto unwired_partial = Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).unwired(S::B);
TEST(Validator, UnwiredMarkerSuppressesOnlyTheMarkedState)
{
    constexpr auto report = validate<unwired_partial>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::EnumeratorNotWired);
}

// A base for the check-6 tables: a flat machine where A, B and C are all declared
// Leaves, so only the Transition conflict under test is in play.
inline constexpr auto flat_base =
    Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::Top).state(S::C, S::Top);

// Check 6: two Transitions on the same (Source, Event) with no Guards to tell them
// apart are ambiguous and rejected.
inline constexpr auto dup_unguarded = flat_base.on(S::A, E::Go, S::B).on(S::A, E::Go, S::C);
TEST(Validator, DuplicateUnguardedTransitionRejected)
{
    constexpr auto report = validate<dup_unguarded>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::DuplicateTransition);
}

// Check 6: two Transitions on the same (Source, Event) carrying the *same* Guard
// are still ambiguous -- the Guards do not distinguish them -- and are rejected.
inline constexpr auto dup_same_guard =
    flat_base.on(S::A, E::Go, S::B, nullptr, &H::g1).on(S::A, E::Go, S::C, nullptr, &H::g1);
TEST(Validator, DuplicateSameGuardTransitionRejected)
{
    constexpr auto report = validate<dup_same_guard>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::DuplicateTransition);
}

// Check 6 positive: two Transitions on the same (Source, Event) with *distinct*
// non-null Guards are a legal guarded/fallback pair and validate cleanly.
inline constexpr auto dup_distinct_guards =
    flat_base.on(S::A, E::Go, S::B, nullptr, &H::g1).on(S::A, E::Go, S::C, nullptr, &H::g2);
TEST(Validator, DistinctGuardsOnSameSourceEventAccepted)
{
    constexpr auto report = validate<dup_distinct_guards>();
    static_assert(report.ok);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.error, ValidationError::None);
}

// Capacity boundary: a table filled to exactly kMaxStates declared States (one
// Top plus kMaxStates-1 children) is at capacity, not over it, so the check-0
// capacity guard must NOT false-positive -- this table validates cleanly.
enum class Big { Top };
inline constexpr auto at_capacity_states = [] {
    auto t = Hsm<H, Big, E>{}.initial(Big::Top, static_cast<Big>(1));
    for (int i = 1; i < static_cast<int>(kMaxStates); ++i)
    {
        t = t.state(static_cast<Big>(i), Big::Top);
    }
    return t;  // stateCount == kMaxStates exactly
}();
TEST(Validator, AtStateCapacityDoesNotFalsePositive)
{
    static_assert(at_capacity_states.stateCount == kMaxStates);
    constexpr auto report = validate<at_capacity_states>();
    EXPECT_NE(report.error, ValidationError::TooManyStates);
}

// Over capacity: one more State than kMaxStates pushes stateCount past the array
// without an OOB write (the builder drops the write, keeps the count), and the
// validator reports the named capacity error.
inline constexpr auto over_capacity_states = [] {
    auto t = Hsm<H, Big, E>{}.initial(Big::Top, static_cast<Big>(1));
    for (int i = 1; i <= static_cast<int>(kMaxStates); ++i)
    {
        t = t.state(static_cast<Big>(i), Big::Top);
    }
    return t;  // stateCount == kMaxStates + 1
}();
TEST(Validator, OverStateCapacityRejected)
{
    static_assert(over_capacity_states.stateCount == kMaxStates + 1);
    constexpr auto report = validate<over_capacity_states>();
    static_assert(!report.ok);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.error, ValidationError::TooManyStates);
}

}  // namespace
}  // namespace eta_hsm::validator_test
