// Build-failure harness for the Logger and Observer template-parameter concepts.
//
// The auto-logging layer routes lines to a Logger (sole contract:
// log(std::string_view)) and a Machine notifies an Observer (onEntry/onExit/
// onInit/onTransition). Both are named concepts, so a type missing or mistyping a
// required member is rejected at the instantiation site with a diagnostic naming
// the concept -- not a deep template error inside the observer's log call or the
// dispatch chain, and not a silent no-op.
//
// One CASE is selected per compile via -DCONCEPTS_CASE=<n>;
// tools/expect_compile_fail.sh compiles each case and asserts the build fails with
// a diagnostic naming the offending concept.

#include <string_view>

#include "eta_hsm/log/auto_logged_machine.hpp"
#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"

namespace {

using namespace eta_hsm;

struct H {};
enum class State { Top, A, B };
enum class Event { Go };

inline constexpr auto table = Hsm<H>{}
                                  .state(State::A, State::Top)
                                  .state(State::B, State::Top)
                                  .initial(State::Top, State::A)
                                  .on(State::A, Event::Go, State::B)
                                  .on(State::B, Event::Go, State::A);

#ifndef CONCEPTS_CASE
#error "define CONCEPTS_CASE (1..3) to select a negative case"
#endif

#if CONCEPTS_CASE == 1
// A Logger missing its sole required member: it spells the sink `write` instead of
// `log(std::string_view)`. The Logger concept rejects it at the AutoLoggedMachine
// instantiation, naming `Logger`, rather than failing deep inside the observer.
struct BadLogger {
    void write(std::string_view) {}
};
using M = AutoLoggedMachine<table, BadLogger>;

#elif CONCEPTS_CASE == 2
// An Observer with a mistyped notify method: `onEnter` instead of `onEntry`. The
// Observer concept rejects it at the Machine instantiation, naming `Observer`,
// rather than letting the misspelled hook silently never fire.
struct BadObserver {
    void onEnter(State) {}  // misspelled: the contract is onEntry
    void onExit(State) {}
    void onInit(State) {}
    void onTransition(State, State, Event) {}
};
using M = Machine<table, BadObserver>;

#elif CONCEPTS_CASE == 3
// An Observer whose notify has the wrong shape: onTransition takes two arguments
// instead of (from, to, event). The Observer concept rejects it at the Machine
// instantiation, naming `Observer`, rather than failing deep inside dispatch where
// the three-argument call is made.
struct BadObserver {
    void onEntry(State) {}
    void onExit(State) {}
    void onInit(State) {}
    void onTransition(State, Event) {}  // wrong arity: should be (from, to, event)
};
using M = Machine<table, BadObserver>;

#else
#error "CONCEPTS_CASE must be 1..3"
#endif

}  // namespace

// Naming the constrained specialization checks its concept; sizeof forces the type
// to be formed, so the unsatisfied constraint is the build failure.
int main() { return static_cast<int>(sizeof(M)); }
