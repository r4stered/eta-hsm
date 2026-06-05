#pragma once

// The cd_player example, ported onto the v2 data-oriented core (issue 0002).
// The whole machine is one `constexpr` table: States with their parent and the
// Top State's Initial Substate, plus flat (Source, Event, Target) Transitions.
// There are no `using`-alias State types and no `switch`-body handlers -- the
// v1 topology (7 States, one level under Top) is expressed as data.

#include <string>

#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm::examples::cd_player {

enum class State { Top, Stopped, Open, Empty, Playing, Paused, Broken };
enum class Event { Play, OpenClose, Stop, CdDetected, Pause, EndPause, Hammer };

// The Host owns no machine state; it supplies Actions and per-State hooks (added
// in later slices). `log` records observable effects for behavioral tests.
struct Player {
    std::string log;
};

// The single source of truth for the cd_player machine.
inline constexpr auto player =
    Hsm<Player, State, Event>{}
        .state(State::Stopped, State::Top)
        .state(State::Open, State::Top)
        .state(State::Empty, State::Top)
        .state(State::Playing, State::Top)
        .state(State::Paused, State::Top)
        .state(State::Broken, State::Top)
        .initial(State::Top, State::Stopped)
        .on(State::Stopped, Event::Play, State::Playing)
        .on(State::Stopped, Event::OpenClose, State::Open)
        .on(State::Open, Event::OpenClose, State::Empty)
        .on(State::Empty, Event::CdDetected, State::Stopped)
        .on(State::Empty, Event::OpenClose, State::Open)
        .on(State::Playing, Event::Stop, State::Stopped)
        .on(State::Playing, Event::Pause, State::Paused)
        .on(State::Playing, Event::OpenClose, State::Open)
        .on(State::Paused, Event::Stop, State::Stopped)
        .on(State::Paused, Event::EndPause, State::Playing)
        .on(State::Paused, Event::OpenClose, State::Open)
        .on(State::Top, Event::Hammer, State::Playing);  // handled by Top from any Leaf

}  // namespace eta_hsm::examples::cd_player
