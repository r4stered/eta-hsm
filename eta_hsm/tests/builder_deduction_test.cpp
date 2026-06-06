// Behavioral tests for the builder's enum-deduction sugar.
//
// The fluent builder can be entered as Hsm<Host>{} and deduces the State enum
// from the first .state / .initial / .unwired call and the Event enum from the
// first .on / .local / .internal call. The deduced result is the SAME table type
// and the SAME value as the explicit Hsm<Host, State, Event>{} build -- the sugar
// is purely a front-end convenience that never changes the produced table. These
// tests pin that equivalence through the public builder interface.

#include <gtest/gtest.h>

#include <type_traits>

#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm::builder_deduction_test {
namespace {

struct H {
    void act() {}
};
enum class S { Top, A, B };
enum class E { Go, Stop };

// The same tiny machine, spelled both ways. The explicit form names all three
// enums; the sugar form deduces State from .state and Event from .on.
constexpr auto explicit_form = Hsm<H, S, E>{}
                                   .state(S::A, S::Top)
                                   .state(S::B, S::Top)
                                   .initial(S::Top, S::A)
                                   .on(S::A, E::Go, S::B)
                                   .on(S::B, E::Stop, S::A);

constexpr auto sugar_form = Hsm<H>{}
                                .state(S::A, S::Top)
                                .state(S::B, S::Top)
                                .initial(S::Top, S::A)
                                .on(S::A, E::Go, S::B)
                                .on(S::B, E::Stop, S::A);

// The deduced builder lands on exactly the explicit table type: Hsm<H, S, E>.
TEST(BuilderDeduction, SugarProducesTheExplicitTableType)
{
    static_assert(std::is_same_v<decltype(sugar_form), decltype(explicit_form)>);
    static_assert(std::is_same_v<decltype(sugar_form), const Hsm<H, S, E>>);
    SUCCEED();
}

// The produced table value is identical, row for row, to the explicit build.
TEST(BuilderDeduction, SugarProducesTheSameTableValue)
{
    static_assert(sugar_form.stateCount == explicit_form.stateCount);
    static_assert(sugar_form.transitionCount == explicit_form.transitionCount);

    EXPECT_EQ(sugar_form.stateCount, explicit_form.stateCount);
    EXPECT_EQ(sugar_form.transitionCount, explicit_form.transitionCount);
    for (std::size_t i = 0; i < explicit_form.stateCount; ++i)
    {
        EXPECT_EQ(sugar_form.states[i].state, explicit_form.states[i].state);
        EXPECT_EQ(sugar_form.states[i].parent, explicit_form.states[i].parent);
        EXPECT_EQ(sugar_form.states[i].isTop, explicit_form.states[i].isTop);
        EXPECT_EQ(sugar_form.states[i].hasInitial, explicit_form.states[i].hasInitial);
        EXPECT_EQ(sugar_form.states[i].initial, explicit_form.states[i].initial);
    }
    for (std::size_t i = 0; i < explicit_form.transitionCount; ++i)
    {
        EXPECT_EQ(sugar_form.transitions[i].source, explicit_form.transitions[i].source);
        EXPECT_EQ(sugar_form.transitions[i].event, explicit_form.transitions[i].event);
        EXPECT_EQ(sugar_form.transitions[i].target, explicit_form.transitions[i].target);
    }
}

// State deduction does not depend on .state coming first: when .initial is the
// opening call (it registers the Top State), the State enum is deduced from it
// just the same, and the rest of the chain proceeds in the deduced stage.
constexpr auto initial_first_explicit =
    Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::Top).on(S::A, E::Go, S::B);
constexpr auto initial_first_sugar =
    Hsm<H>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::Top).on(S::A, E::Go, S::B);

TEST(BuilderDeduction, InitialAsFirstCallDeducesState)
{
    static_assert(std::is_same_v<decltype(initial_first_sugar), const Hsm<H, S, E>>);
    static_assert(initial_first_sugar.stateCount == initial_first_explicit.stateCount);
    static_assert(initial_first_sugar.transitionCount == initial_first_explicit.transitionCount);
    EXPECT_EQ(initial_first_sugar.states[0].state, initial_first_explicit.states[0].state);
    EXPECT_TRUE(initial_first_sugar.states[0].isTop);
    EXPECT_TRUE(initial_first_sugar.states[0].hasInitial);
}

// A Transition as the opening call deduces BOTH enums at once at stage 0: the
// State enum from the source/target and the Event enum from the event. The
// result still lands on the explicit table type and value.
constexpr auto transition_first_explicit = Hsm<H, S, E>{}.on(S::A, E::Go, S::B).initial(S::Top, S::A);
constexpr auto transition_first_sugar = Hsm<H>{}.on(S::A, E::Go, S::B).initial(S::Top, S::A);

TEST(BuilderDeduction, TransitionAsFirstCallDeducesBothEnums)
{
    static_assert(std::is_same_v<decltype(transition_first_sugar), const Hsm<H, S, E>>);
    static_assert(transition_first_sugar.transitionCount == transition_first_explicit.transitionCount);
    EXPECT_EQ(transition_first_sugar.transitions[0].source, S::A);
    EXPECT_EQ(transition_first_sugar.transitions[0].event, E::Go);
    EXPECT_EQ(transition_first_sugar.transitions[0].target, S::B);
}

// .unwired can open the chain too: it deduces the State enum and leaves the
// builder in the State-known stage (no Event yet), recording the opt-out.
constexpr auto unwired_first = Hsm<H>{}.unwired(S::B).state(S::A, S::Top).initial(S::Top, S::A);

TEST(BuilderDeduction, UnwiredAsFirstCallDeducesState)
{
    static_assert(std::is_same_v<decltype(unwired_first), const Hsm<H, S, Deduce>>);
    static_assert(unwired_first.unwiredCount == 1);
    EXPECT_EQ(unwired_first.unwiredStates[0], S::B);
}

// .internal is an Event-bearing call, so it can be the one that deduces the Event
// enum: opening the Transition phase with .internal lands on the explicit table
// type and records an Internal Transition, just as the explicit form would.
constexpr auto internal_first_explicit =
    Hsm<H, S, E>{}.state(S::A, S::Top).initial(S::Top, S::A).internal(S::A, E::Go, &H::act);
constexpr auto internal_first_sugar = Hsm<H>{}.state(S::A, S::Top).initial(S::Top, S::A).internal(S::A, E::Go, &H::act);

TEST(BuilderDeduction, InternalAsFirstEventCallDeducesEvent)
{
    static_assert(std::is_same_v<decltype(internal_first_sugar), const Hsm<H, S, E>>);
    static_assert(internal_first_sugar.transitionCount == internal_first_explicit.transitionCount);
    EXPECT_TRUE(internal_first_sugar.transitions[0].internal);
    EXPECT_EQ(internal_first_sugar.transitions[0].source, S::A);
    EXPECT_EQ(internal_first_sugar.transitions[0].event, E::Go);
}

// .local likewise deduces the Event enum when it opens the Transition phase, and
// records a Local Transition matching the explicit build.
constexpr auto local_first_explicit =
    Hsm<H, S, E>{}.state(S::A, S::Top).state(S::B, S::Top).initial(S::Top, S::A).local(S::A, E::Go, S::B);
constexpr auto local_first_sugar =
    Hsm<H>{}.state(S::A, S::Top).state(S::B, S::Top).initial(S::Top, S::A).local(S::A, E::Go, S::B);

TEST(BuilderDeduction, LocalAsFirstEventCallDeducesEvent)
{
    static_assert(std::is_same_v<decltype(local_first_sugar), const Hsm<H, S, E>>);
    static_assert(local_first_sugar.transitionCount == local_first_explicit.transitionCount);
    EXPECT_TRUE(local_first_sugar.transitions[0].local);
    EXPECT_EQ(local_first_sugar.transitions[0].target, S::B);
}

}  // namespace
}  // namespace eta_hsm::builder_deduction_test
