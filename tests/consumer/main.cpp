// Compiles and runs only if the package exposed the full public header surface
// and propagated C++26 + -freflection + -fcontracts from the imported
// EtaHsm::eta_hsm INTERFACE target. Building a real Machine<> over the machine/
// headers is the load-bearing check: it fails to compile if the install set ever
// stops shipping machine/ (and, transitively, the headers it pulls in).
#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"

#include <cstdio>

namespace {

enum class State { Top, Locked, Unlocked };
enum class Event { Coin, Push };

// A turnstile: a Coin unlocks it; a Push while Unlocked admits one rider and
// re-locks. The Host counts admitted riders so the run is observable.
struct Turnstile {
  int admitted{0};
  constexpr void admit() { ++admitted; }
};

inline constexpr auto turnstile = eta_hsm::Hsm<Turnstile>{}
                                      .state(State::Locked, State::Top)
                                      .state(State::Unlocked, State::Top)
                                      .initial(State::Top, State::Locked)
                                      .on(State::Locked, Event::Coin, State::Unlocked)
                                      .on(State::Unlocked, Event::Push, State::Locked, &Turnstile::admit);

}  // namespace

int main() {
  eta_hsm::Machine<turnstile> m;

  if (m.identify() != State::Locked) {
    std::puts("eta_hsm consumer FAIL: machine did not rest in Locked");
    return 1;
  }

  m.dispatch(Event::Push);  // ignored while Locked
  m.dispatch(Event::Coin);  // -> Unlocked
  m.dispatch(Event::Push);  // admit one rider, -> Locked

  if (m.identify() != State::Locked || m.host().admitted != 1) {
    std::puts("eta_hsm consumer FAIL: turnstile did not admit exactly one rider");
    return 1;
  }

  std::printf("eta_hsm consumer OK: turnstile admitted %d rider, rests in Locked\n",
              m.host().admitted);
  return 0;
}
