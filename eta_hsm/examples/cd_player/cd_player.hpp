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

// The Host owns no machine state; it supplies Actions (run on a Transition,
// between Exit and Entry) as ordinary member functions. `log` records observable
// effects so behavioral tests can assert ordering.
struct Player {
    std::string log;

    void start_playback() { log += "start_playback;"; }
    void open_drawer() { log += "open_drawer;"; }
    void close_drawer() { log += "close_drawer;"; }
    void store_cd_info() { log += "store_cd_info;"; }
    void stop_playback() { log += "stop_playback;"; }
    void pause_playback() { log += "pause_playback;"; }
    void resume_playback() { log += "resume_playback;"; }
    void stop_and_open() { log += "stop_and_open;"; }

    // Per-State Entry/Exit hooks, auto-detected by reflection: the Host writes
    // only the ones it needs. Open/Empty/Broken deliberately have none, so
    // entering/leaving them fires nothing.
    void entry_Top() { log += "+Top;"; }
    void exit_Top() { log += "-Top;"; }
    void entry_Stopped() { log += "+Stopped;"; }
    void exit_Stopped() { log += "-Stopped;"; }
    void entry_Playing() { log += "+Playing;"; }
    void exit_Playing() { log += "-Playing;"; }
    void entry_Paused() { log += "+Paused;"; }
    void exit_Paused() { log += "-Paused;"; }
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
        .on(State::Stopped, Event::Play, State::Playing, &Player::start_playback)
        .on(State::Stopped, Event::OpenClose, State::Open, &Player::open_drawer)
        .on(State::Open, Event::OpenClose, State::Empty, &Player::close_drawer)
        .on(State::Empty, Event::CdDetected, State::Stopped, &Player::store_cd_info)
        .on(State::Empty, Event::OpenClose, State::Open, &Player::open_drawer)
        .on(State::Playing, Event::Stop, State::Stopped, &Player::stop_playback)
        .on(State::Playing, Event::Pause, State::Paused, &Player::pause_playback)
        .on(State::Playing, Event::OpenClose, State::Open, &Player::stop_and_open)
        .on(State::Paused, Event::Stop, State::Stopped, &Player::stop_playback)
        .on(State::Paused, Event::EndPause, State::Playing, &Player::resume_playback)
        .on(State::Paused, Event::OpenClose, State::Open, &Player::stop_and_open)
        .on(State::Top, Event::Hammer, State::Playing);  // handled by Top from any Leaf

}  // namespace eta_hsm::examples::cd_player
