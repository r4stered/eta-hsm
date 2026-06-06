#pragma once

// Auto-logging layer. It wraps a Machine with a pluggable Logger and emits
// human-readable lines for Transitions, Entries, Exits, and inits, with State and
// Event names from enum reflection. The verbosity levels and line wording are:
//
//   0  nothing
//   1  Transitions          "<name> HSM transitioning from <State> to <State> due to <Event>"
//   2  + Entry / Exit       "<name> HSM entering state <State>" / "... exiting state ..."
//   3  + init               "<name> HSM initializing state <State>"
//
// The Logger is a template parameter so consumers route lines to their own sink
// (e.g. a ROS logger); its sole requirement -- enforced by the Logger concept
// below -- is a `log(std::string_view)` member that receives one finished line at
// a time.

#include <string>
#include <string_view>
#include <utility>

#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm {

// A Logger is any sink with a single `log(std::string_view)` member: one finished
// line in, nothing out. This is the whole contract the auto-logging layer asks of
// the type it routes lines to. Constraining the Logger template parameter by it
// turns a missing or mistyped log() into a diagnostic naming `Logger` at the
// instantiation site, rather than a deep error inside the observer's log call.
template <class L>
concept Logger = requires(L& l, std::string_view line) { l.log(line); };

// The Observer the auto-logging layer installs into a Machine. It renders each
// notify into a finished line and hands it to the Logger, gated by verbosity. It
// holds a Logger* (not a value) so the AutoLoggedMachine owns the one sink and
// the Observer copy the Machine stores points back at it.
template <class State, class Event, Logger LoggerT>
class LoggingObserver {
public:
    LoggingObserver() = default;
    LoggingObserver(std::string name, LoggerT* logger, unsigned verbosity)
        : name_{std::move(name)}, logger_{logger}, verbosity_{verbosity}
    {}

    void onEntry(State state)
    {
        if (logger_ != nullptr && verbosity_ >= 2)
        {
            logger_->log(name_ + " HSM entering state " + nameOf(state));
        }
    }

    void onExit(State state)
    {
        if (logger_ != nullptr && verbosity_ >= 2)
        {
            logger_->log(name_ + " HSM exiting state " + nameOf(state));
        }
    }

    void onInit(State state)
    {
        if (logger_ != nullptr && verbosity_ >= 3)
        {
            logger_->log(name_ + " HSM initializing state " + nameOf(state));
        }
    }

    void onTransition(State from, State to, Event event)
    {
        if (logger_ != nullptr && verbosity_ >= 1)
        {
            logger_->log(name_ + " HSM transitioning from " + nameOf(from) + " to " + nameOf(to) + " due to " +
                         nameOf(event));
        }
    }

private:
    template <class Enum>
    static std::string nameOf(Enum value)
    {
        auto const name = enum_name(value);
        return name ? std::string{*name} : std::string{"?"};
    }

    std::string name_{};
    LoggerT* logger_{nullptr};
    unsigned verbosity_{0};
};

// Opt-in logging wrapper around a Machine<Table>. Construct it with a machine
// name, a Logger sink, and a verbosity level; drive it exactly like a Machine
// (dispatch / during / identify / isInSubstateOf / host) and the wrapped Machine
// emits lines through the Logger as it runs.
template <auto Table, Logger LoggerT>
class AutoLoggedMachine {
public:
    using State = typename decltype(Table)::State;
    using Event = typename decltype(Table)::Event;

    AutoLoggedMachine(std::string name, LoggerT& logger, unsigned verbosity = 3)
        : machine_{Observer{std::move(name), &logger, verbosity}}
    {}

    void dispatch(Event event) { machine_.dispatch(event); }
    void during() { machine_.during(); }
    template <class Input>
    void during(const Input& input)
    {
        machine_.during(input);
    }

    State identify() const { return machine_.identify(); }
    bool isInSubstateOf(State ancestor) const { return machine_.isInSubstateOf(ancestor); }

    auto& host() { return machine_.host(); }
    const auto& host() const { return machine_.host(); }

private:
    using Observer = LoggingObserver<State, Event, LoggerT>;
    Machine<Table, Observer> machine_;
};

}  // namespace eta_hsm
