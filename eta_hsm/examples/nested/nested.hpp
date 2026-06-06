#pragma once

// A multi-level example exercising the full hierarchy:
// composite States nested two deep, cross-level Transitions, parent deferral to
// an ancestor handler, and the External-vs-Local distinction on a parent/child
// Transition. The whole machine is one `constexpr` table, like cd_player.
//
// State tree (Top's Initial Substate is A; A's is A1; B's is B1; B1's is B1a):
//
//   Top
//   |- A            (Composite, initial -> A1)
//   |  |- A1        (Leaf)
//   |  '- A2        (Leaf)
//   '- B            (Composite, initial -> B1)
//      |- B1        (Composite, initial -> B1a)
//      |  |- B1a    (Leaf)
//      |  '- B1b    (Leaf)
//      '- B2        (Leaf)
//
// So the resting Leaf at construction is A1, reached by drilling Top -> A -> A1.

#include <string>

#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm::examples::nested {

enum class State { Top, A, A1, A2, B, B1, B1a, B1b, B2 };
enum class Event {
    Go,  // A1 -> B1b: cross-composite Transition into a deep Leaf
    Within,  // B1a -> B1b: sibling Transition inside B1
    Up,  // B1a -> B2: cross-level Transition inside B
    ToB,  // A1 -> B: Target is a Composite; settles in B1a by drilling
    Back,  // B1a -> A: Target Composite in the other branch; settles in A1
    Reset,  // handled by Top from any Leaf: -> A (drills to A1)
    ReenterExt,  // A -> A1, External: exits and re-enters A
    ReenterLoc,  // A -> A1, Local: does NOT exit/re-enter A
};

// The Host records every Action and per-State Entry/Exit hook into `log` so tests
// can assert the exact ordered chain a Transition runs.
struct Model {
    std::string log;

    constexpr void act_go() { log += "go;"; }

    constexpr void entry_Top() { log += "+Top;"; }
    constexpr void exit_Top() { log += "-Top;"; }
    constexpr void entry_A() { log += "+A;"; }
    constexpr void exit_A() { log += "-A;"; }
    constexpr void entry_A1() { log += "+A1;"; }
    constexpr void exit_A1() { log += "-A1;"; }
    constexpr void entry_A2() { log += "+A2;"; }
    constexpr void exit_A2() { log += "-A2;"; }
    constexpr void entry_B() { log += "+B;"; }
    constexpr void exit_B() { log += "-B;"; }
    constexpr void entry_B1() { log += "+B1;"; }
    constexpr void exit_B1() { log += "-B1;"; }
    constexpr void entry_B1a() { log += "+B1a;"; }
    constexpr void exit_B1a() { log += "-B1a;"; }
    constexpr void entry_B1b() { log += "+B1b;"; }
    constexpr void exit_B1b() { log += "-B1b;"; }
    constexpr void entry_B2() { log += "+B2;"; }
    constexpr void exit_B2() { log += "-B2;"; }
};

inline constexpr auto model = Hsm<Model>{}
                                  .state(State::A, State::Top)
                                  .state(State::A1, State::A)
                                  .state(State::A2, State::A)
                                  .state(State::B, State::Top)
                                  .state(State::B1, State::B)
                                  .state(State::B1a, State::B1)
                                  .state(State::B1b, State::B1)
                                  .state(State::B2, State::B)
                                  .initial(State::Top, State::A)
                                  .initial(State::A, State::A1)
                                  .initial(State::B, State::B1)
                                  .initial(State::B1, State::B1a)
                                  .on(State::A1, Event::Go, State::B1b, &Model::act_go)
                                  .on(State::B1a, Event::Within, State::B1b)
                                  .on(State::B1a, Event::Up, State::B2)
                                  .on(State::A1, Event::ToB, State::B)
                                  .on(State::B1a, Event::Back, State::A)
                                  .on(State::Top, Event::Reset, State::A)
                                  .on(State::A, Event::ReenterExt, State::A1)
                                  .local(State::A, Event::ReenterLoc, State::A1);

}  // namespace eta_hsm::examples::nested
