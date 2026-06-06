#pragma once

// The cd_player example.
// The whole machine is one `constexpr` table: States with their parent and the
// Top State's Initial Substate, plus flat (Source, Event, Target) Transitions.
// There are no `using`-alias State types and no `switch`-body handlers -- the
// topology (7 States, one level under Top) is expressed as data.

#include <string>

#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm::examples::cd_player {

enum class State { Top, Stopped, Open, Empty, Playing, Paused, Broken };
enum class Event { Play, OpenClose, Stop, CdDetected, Pause, EndPause, Hammer, VolumeUp, Next };

// The Host owns no machine state; it supplies Actions (run on a Transition,
// between Exit and Entry) as ordinary member functions. `log` records observable
// effects so behavioral tests can assert ordering.
struct Player {
    std::string log;
    bool drawer_stuck{false};  // backs the drawer_jammed Guard
    int volume{0};  // mutated by the turn_up Internal-Transition Action

    // A Guard: `bool (Player::*)() const`. Whacking a jammed drawer with the
    // Hammer frees it; an un-jammed drawer defers the Hammer to Top.
    constexpr bool drawer_jammed() const { return drawer_stuck; }

    constexpr void start_playback() { log += "start_playback;"; }
    constexpr void open_drawer() { log += "open_drawer;"; }
    constexpr void close_drawer() { log += "close_drawer;"; }
    constexpr void store_cd_info() { log += "store_cd_info;"; }
    constexpr void stop_playback() { log += "stop_playback;"; }
    constexpr void pause_playback() { log += "pause_playback;"; }
    constexpr void resume_playback() { log += "resume_playback;"; }
    constexpr void stop_and_open() { log += "stop_and_open;"; }
    constexpr void turn_up()
    {
        ++volume;
        log += "turn_up;";
    }
    constexpr void next_track() { log += "next_track;"; }

    // Per-State Entry/Exit hooks, auto-detected by reflection: the Host writes
    // only the ones it needs. Open/Empty/Broken deliberately have none, so
    // entering/leaving them fires nothing.
    constexpr void entry_Top() { log += "+Top;"; }
    constexpr void exit_Top() { log += "-Top;"; }
    constexpr void entry_Stopped() { log += "+Stopped;"; }
    constexpr void exit_Stopped() { log += "-Stopped;"; }
    constexpr void entry_Playing() { log += "+Playing;"; }
    constexpr void exit_Playing() { log += "-Playing;"; }
    constexpr void entry_Paused() { log += "+Paused;"; }
    constexpr void exit_Paused() { log += "-Paused;"; }
};

// The single source of truth for the cd_player machine. The builder is entered as
// Hsm<Player>{}: the State enum is deduced from the first .state and the Event
// enum from the first .on, so the machine's two enums are named once each, at
// their first use, rather than spelled again in the builder type.
inline constexpr auto player =
    Hsm<Player>{}
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
        // Guarded Transition: Hammer while Stopped frees the drawer (-> Open) only
        // when it is jammed; otherwise the Guard is false and Hammer defers to Top.
        .on(State::Stopped, Event::Hammer, State::Open, &Player::open_drawer, &Player::drawer_jammed)
        // Internal Transition: VolumeUp while Playing runs turn_up only -- the
        // machine stays in Playing and no Exit/Entry fires.
        .internal(State::Playing, Event::VolumeUp, &Player::turn_up)
        // Self-Transition: Next while Playing re-enters Playing, so its Exit and
        // Entry fire around next_track (Source == Target, unlike an Internal).
        .on(State::Playing, Event::Next, State::Playing, &Player::next_track)
        .on(State::Top, Event::Hammer, State::Playing);  // handled by Top from any Leaf

}  // namespace eta_hsm::examples::cd_player
