#pragma once

// The example_control example: a deep-hierarchy machine whose Leaf States nest
// two levels under Top, so a Transition out of a Leaf can run an Exit/Entry chain
// through an intermediate Composite ancestor (the least-common-ancestor case a
// single-level machine cannot reach).
//
// State tree (Top's Initial Substate is Awake; Awake's is Sober):
//
//   Top
//   |- Awake          (Composite, initial -> Sober)
//   |  |- Sober       (Leaf)
//   |  |- Drunk       (Leaf)
//   |  '- Bored       (Leaf)
//   '- Unconscious    (Leaf)
//
// So the resting Leaf at construction is Sober, reached by drilling
// Top -> Awake -> Sober.
//
// The Host carries a continuous Blood-Alcohol-Content value the drinking Actions
// raise and the During tick metabolizes. Drinking is handled at two levels: Awake
// raises the BAC for any of its substates (an Internal Transition that defers up
// the chain), while Sober additionally watches for the BAC crossing the
// intoxication threshold and tips into Drunk -- a Guard that, when false, lets the
// Event keep deferring to Awake's BAC-raising handler.

#include <string>

#include "eta_hsm/machine/hsm.hpp"

namespace eta_hsm::examples::example_control {

enum class State { Top, Awake, Sober, Drunk, Bored, Unconscious };
enum class Event { DrinkBeer, DrinkWhiskey, LookAtWatch, PassOut };

// The intoxication threshold: once BAC reaches it, the next drink while Sober
// transitions into Drunk instead of merely raising the BAC.
inline constexpr float kDrunkThreshold = 0.08f;
inline constexpr float kBeer = 0.025f;  // BAC a beer adds
inline constexpr float kWhiskey = 0.05f;  // BAC a whiskey adds

// The Host owns the continuous state (BAC, awake-ness) and supplies Actions,
// Guards, and per-State hooks. `log` records observable effects in order so
// behavioral tests can assert the exact Exit/Action/Entry chain.
struct Drinker {
    std::string log;
    float bac{0.0f};
    bool awake{false};  // toggled by Awake's Entry/Exit, mirroring continuous-state control

    // Raise BAC by `amt`, clamping at zero so metabolizing past sober stays sober.
    void increase_bac(float amt)
    {
        bac += amt;
        if (bac < 0.0f)
        {
            bac = 0.0f;
        }
    }

    // Actions, run on a Transition between Exit and Entry. The drinking Actions
    // raise the BAC and are shared by Awake's BAC-only handlers and Sober's
    // tip-into-Drunk Transitions.
    void drink_beer()
    {
        increase_bac(kBeer);
        log += "beer;";
    }
    void drink_whiskey()
    {
        increase_bac(kWhiskey);
        log += "whiskey;";
    }
    void keep_partying() { log += "keep_partying;"; }
    void pass_out() { log += "passout;"; }

    // Guards: a drink tips Sober into Drunk only when it would carry the BAC to
    // the intoxication threshold. When the Guard is false the Event keeps
    // deferring up to Awake, which raises the BAC without changing State.
    bool tipsy_after_beer() const { return bac + kBeer >= kDrunkThreshold; }
    bool tipsy_after_whiskey() const { return bac + kWhiskey >= kDrunkThreshold; }

    // During ticks: each update metabolizes a little alcohol for the current Leaf,
    // independent of any Event. No Transition runs and no Exit/Entry fires.
    void during_Sober() { increase_bac(-0.01f); }
    void during_Drunk() { increase_bac(-0.01f); }

    // Per-State Entry/Exit hooks, auto-detected by reflection. Awake additionally
    // drives the awake flag, showing Entry/Exit manipulating continuous state.
    void entry_Top() { log += "+Top;"; }
    void exit_Top() { log += "-Top;"; }
    void entry_Awake()
    {
        log += "+Awake;";
        awake = true;
    }
    void exit_Awake()
    {
        log += "-Awake;";
        awake = false;
    }
    void entry_Sober() { log += "+Sober;"; }
    void exit_Sober() { log += "-Sober;"; }
    void entry_Drunk() { log += "+Drunk;"; }
    void exit_Drunk() { log += "-Drunk;"; }
    void entry_Bored() { log += "+Bored;"; }
    void exit_Bored() { log += "-Bored;"; }
    void entry_Unconscious() { log += "+Unconscious;"; }
    void exit_Unconscious() { log += "-Unconscious;"; }
};

// The single source of truth for the example_control machine. The builder is
// entered as Hsm<Drinker>{}: the State enum is deduced from the first .state and
// the Event enum from the first .internal, so each enum is named once at first use
// rather than spelled again in the builder type.
inline constexpr auto drinker =
    Hsm<Drinker>{}
        .state(State::Awake, State::Top)
        .state(State::Sober, State::Awake)
        .state(State::Drunk, State::Awake)
        .state(State::Bored, State::Awake)
        .state(State::Unconscious, State::Top)
        .initial(State::Top, State::Awake)
        .initial(State::Awake, State::Sober)
        // Awake handles drinking for any of its substates: an Internal Transition
        // that raises the BAC without changing State. A substate that does not
        // handle a drink defers up to here.
        .internal(State::Awake, Event::DrinkBeer, &Drinker::drink_beer)
        .internal(State::Awake, Event::DrinkWhiskey, &Drinker::drink_whiskey)
        // Passing out is handled at Awake too, so it fires from any Awake substate:
        // a cross-level Transition that exits the Leaf and Awake and comes to rest
        // in Unconscious, the other child of Top.
        .on(State::Awake, Event::PassOut, State::Unconscious, &Drinker::pass_out)
        // Sober additionally tips into Drunk when a drink would cross the
        // intoxication threshold (a Guarded Transition); otherwise the drink Event
        // defers to Awake's BAC-only handler above.
        .on(State::Sober, Event::DrinkBeer, State::Drunk, &Drinker::drink_beer, &Drinker::tipsy_after_beer)
        .on(State::Sober, Event::DrinkWhiskey, State::Drunk, &Drinker::drink_whiskey, &Drinker::tipsy_after_whiskey)
        // Sober looks at the watch and gets Bored: a sibling Transition within Awake.
        .on(State::Sober, Event::LookAtWatch, State::Bored)
        // Drunk overrides LookAtWatch with an Internal Transition: it keeps partying
        // rather than getting Bored, so no State change and no Exit/Entry.
        .internal(State::Drunk, Event::LookAtWatch, &Drinker::keep_partying);

}  // namespace eta_hsm::examples::example_control
