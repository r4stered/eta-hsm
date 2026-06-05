// Build-failure harness for the maximal compile-time validator (issue 0006).
//
// This translation unit is COMPILED ON PURPOSE TO FAIL: instantiating a Machine
// over an ill-formed table must reject the program with a static_assert whose
// message names the offending element. One CASE is selected per compile via
// -DVALIDATOR_CASE=<n>; tools/expect_compile_fail.sh compiles each case and
// asserts the build fails with the matching offender text. This proves the half a
// pure-runtime test cannot: that validation really fires at instantiation and the
// diagnostic is legible. The companion validator_test.cpp covers the logic.
//
// Forcing the class to a complete type via sizeof instantiates Machine<Table>,
// which is where the validator's static_assert lives.

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"

namespace {

using namespace eta_hsm;

struct H {
    bool g1() const { return true; }
};
enum class S { Top, A, B, C };
enum class E { Go };

#ifndef VALIDATOR_CASE
#error "define VALIDATOR_CASE (1..6) to select a negative case"
#endif

#if VALIDATOR_CASE == 1
// Check 1: no Top State (only a .state call, never the .initial that makes a Top).
inline constexpr auto table = Hsm<H, S, E>{}.state(S::A, S::Top);

#elif VALIDATOR_CASE == 2
// Check 2: B's parent C is never declared.
inline constexpr auto table = Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::C).unwired(S::C);

#elif VALIDATOR_CASE == 3
// Check 3: Composite A's Initial Substate C is not one of A's children.
inline constexpr auto table =
    Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).state(S::B, S::A).state(S::C, S::Top).initial(S::A, S::C);

#elif VALIDATOR_CASE == 4
// Check 4: the (A, Go) Transition targets undeclared State C.
inline constexpr auto table =
    Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top).unwired(S::B).unwired(S::C).on(S::A, E::Go, S::C);

#elif VALIDATOR_CASE == 5
// Check 5: enumerators B and C are wired nowhere and not marked .unwired.
inline constexpr auto table = Hsm<H, S, E>{}.initial(S::Top, S::A).state(S::A, S::Top);

#elif VALIDATOR_CASE == 6
// Check 6: two Transitions on (A, Go) with no distinct Guards.
inline constexpr auto table = Hsm<H, S, E>{}
                                  .initial(S::Top, S::A)
                                  .state(S::A, S::Top)
                                  .state(S::B, S::Top)
                                  .state(S::C, S::Top)
                                  .on(S::A, E::Go, S::B)
                                  .on(S::A, E::Go, S::C);

#else
#error "VALIDATOR_CASE must be 1..6"
#endif

}  // namespace

int main() { return static_cast<int>(sizeof(eta_hsm::Machine<table>)); }
