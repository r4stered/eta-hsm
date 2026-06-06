// Build-failure harness for the run_hook hook-detection contract. The
// fixed-arity hooks -- entry_<Name>, exit_<Name>, during_<Name>
// -- are always called with an EMPTY argument pack, so a Host member that matches
// the <prefix>_<Name> name but takes the WRONG arity/type is a programmer error
// and MUST be a hard compile error, not a silent no-op. (Only the variadic
// stateUpdate tick is allowed to skip a name-matching hook whose shape does not
// fit the forwarded Input -- that intentional skip is the whole point of 0005 and
// is exercised behaviorally by during_test.cpp, not here.)
//
// One CASE is selected per compile via -DRUNHOOK_CASE=<n>;
// tools/expect_compile_fail.sh compiles each case and asserts the build fails
// with a diagnostic naming the offending call. Constructing the Machine runs the
// initial Entry chain, which calls run_hook<"entry"> -- that is where a misshapen
// entry_<Name>(int) hook is detected and rejected.

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"

namespace {

using namespace eta_hsm;

enum class State { Top, Running, Idle };
enum class Event { Toggle };

template <class H>
constexpr auto make_table()
{
    return Hsm<H>{}
        .state(State::Running, State::Top)
        .state(State::Idle, State::Top)
        .initial(State::Top, State::Running)
        .on(State::Running, Event::Toggle, State::Idle)
        .on(State::Idle, Event::Toggle, State::Running);
}

#ifndef RUNHOOK_CASE
#error "define RUNHOOK_CASE (1) to select a negative case"
#endif

#if RUNHOOK_CASE == 1
// A fixed-arity entry hook declared with the wrong arity: entry_<Name> is called
// with no arguments, but this one demands an int. Before the run_hook fix this
// silently compiled to a no-op (the requires-guard swallowed it); it must now be
// a hard "no matching function" error at the spliced call.
struct H {
    void entry_Running(int) {}
};

#else
#error "RUNHOOK_CASE must be 1"
#endif

inline constexpr auto table = make_table<H>();

}  // namespace

// Instantiating and constructing the Machine runs the initial Entry chain, which
// calls run_hook<"entry"> for Top's initial Leaf (Running) -- the misshapen
// entry_Running(int) hook is reached there.
int main()
{
    eta_hsm::Machine<table> m;
    return static_cast<int>(m.identify() == State::Running);
}
