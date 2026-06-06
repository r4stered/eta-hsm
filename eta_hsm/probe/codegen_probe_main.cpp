// Codegen probe for the README's event-path performance claims.
//
// A deliberately non-allocating machine whose dispatch is emitted as one
// findable symbol, so two structural properties can be read off the -O2
// disassembly by tools/codegen_gate.sh:
//
//   * zero heap allocation on the event path, and
//   * a flat scan with no function-pointer table walk (the pointer-to-member
//     Action/Guard calls devirtualize against the constexpr table).
//
// The Host holds only int counters and a bool guard -- no std::string -- so any
// allocation in the disassembly would be the machine's, not the Host's.
//
// Defining CODEGEN_PROBE_PLANT_HEAP makes an Action escape a `new` onto the
// event path: the gate compiles this variant to prove its no-heap matcher
// actually catches a regression rather than passing vacuously.

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"

using namespace eta_hsm;

struct Counter {
    int ticks = 0;
    int hops = 0;
    bool armed = false;
    int* sink = nullptr;  // escape target for the planted-heap variant

    void bump()  // Action on an Internal Transition
    {
#ifdef CODEGEN_PROBE_PLANT_HEAP
        sink = new int(ticks);  // escapes, so -O2 cannot elide the allocation
#else
        ++ticks;
#endif
    }
    void hop() { ++hops; }  // Action on a state-changing Transition
    bool is_armed() const { return armed; }  // Guard
};

enum class State { Top, A, B };
enum class Event { Go, Tick, Arm };

inline constexpr auto probe = Hsm<Counter>{}
                                  .state(State::A, State::Top)
                                  .state(State::B, State::Top)
                                  .initial(State::Top, State::A)
                                  .on(State::A, Event::Go, State::B, &Counter::hop, &Counter::is_armed)
                                  .internal(State::A, Event::Tick, &Counter::bump);

using ProbeMachine = Machine<probe>;

// Emit dispatch (+ inlined callees) as one out-of-line symbol to disassemble.
[[gnu::noinline, gnu::used]] void probe_dispatch(ProbeMachine& m, Event e) { m.dispatch(e); }
