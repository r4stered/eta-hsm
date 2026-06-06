// Build-failure harness for the builder's enum-deduction sugar.
//
// Once Hsm<Host>{} deduces an enum, that slot is fixed: the State enum from the
// first .state/.initial/.unwired and the Event enum from the first
// .on/.local/.internal. A later call that names a SECOND, different enum in that
// slot must be a clear compile error, never a silent re-deduction or a coerced
// value. Each case below mixes a foreign enum into an already-deduced slot; the
// builder methods take the concrete deduced enum, so the call has no viable
// conversion and the diagnostic names the offending foreign enum.
//
// One CASE is selected per compile via -DBUILDER_CASE=<n>;
// tools/expect_compile_fail.sh compiles each case and asserts the build fails
// with a diagnostic naming the offender. The companion builder_deduction_test.cpp
// covers the positive side (deduction lands on the explicit table type/value).

#include "eta_hsm/machine/hsm.hpp"

namespace {

using namespace eta_hsm;

struct H {};
enum class S { Top, A, B };
enum class E { Go };
enum class OtherState { X, Y };
enum class OtherEvent { Boom };

#ifndef BUILDER_CASE
#error "define BUILDER_CASE (1..2) to select a negative case"
#endif

#if BUILDER_CASE == 1
// State slot fixed by the first .state(S, S); a second .state naming the foreign
// OtherState enum cannot convert to the deduced State enum.
inline constexpr auto table = Hsm<H>{}.state(S::A, S::Top).state(OtherState::X, OtherState::Y);

#elif BUILDER_CASE == 2
// Event slot fixed by the first .on(..., E, ...); a second .on naming the foreign
// OtherEvent enum cannot convert to the deduced Event enum.
inline constexpr auto table =
    Hsm<H>{}.state(S::A, S::Top).initial(S::Top, S::A).on(S::A, E::Go, S::A).on(S::A, OtherEvent::Boom, S::A);

#else
#error "BUILDER_CASE must be 1..2"
#endif

}  // namespace

int main() { return static_cast<int>(sizeof(table)); }
