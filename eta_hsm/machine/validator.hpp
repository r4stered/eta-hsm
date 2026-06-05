#pragma once

// Maximal compile-time validator for a machine table (issue 0006). A single
// entry point -- validate<Table>() -- runs every well-formedness check over the
// constexpr table (built with Hsm, see hsm.hpp) and returns a ValidationReport.
// The report carries a P2741 user-generated message naming the offending element,
// so a failing machine rejected by Machine's static_assert (machine.hpp) reads as
// a clear diagnostic rather than a deep template error.
//
// The logic is a pure consteval function so it is testable directly: a positive
// machine yields {ok=true}, each ill-formed machine yields {ok=false, error=...}
// with the matching ValidationError. Machine<Table> turns the same report into a
// static_assert that fires automatically on instantiation.

#include <array>
#include <cstddef>
#include <string_view>

#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm {

// The well-formedness faults the validator can report. Checks run in this order
// and the first failure wins, so a report carries exactly one error.
enum class ValidationError {
    None,
    NoTopState,  // check 1: zero Top States
    MultipleTopStates,  // check 1: more than one Top State
    MissingParent,  // check 2: a non-Top State's parent is not declared
    InitialNotChild,  // check 3: a Composite State lacks a valid Initial Substate
    TargetNotDeclared,  // check 4: a Transition Target is not a declared State
    EnumeratorNotWired,  // check 5: a State enumerator is neither declared nor .unwired
    DuplicateTransition,  // check 6: same (Source, Event) without distinct Guards
};

// The result of validating a table. `text`/`len` hold a human-readable message
// naming the offending element; size()/data() make the report itself usable as a
// P2741 static_assert message operand.
struct ValidationReport {
    bool ok{true};
    ValidationError error{ValidationError::None};
    std::array<char, 192> text{};
    std::size_t len{0};

    constexpr std::size_t size() const { return len; }
    constexpr const char* data() const { return text.data(); }
};

namespace detail {

// A small fixed-capacity text accumulator the checks build their offender-naming
// message into. Truncates silently if a message would exceed the buffer (the
// report's buffer is sized to hold any message the checks produce).
struct MsgBuf {
    std::array<char, 192> text{};
    std::size_t len{0};
    constexpr void operator+=(std::string_view s)
    {
        for (char c : s)
        {
            if (len + 1 < text.size())
            {
                text[len++] = c;
            }
        }
    }
};

// The enumerator identifier of `v`, or a placeholder if `v` is not a declared
// enumerator (so a message is always printable, even for a bogus value).
template <class E>
constexpr std::string_view name(E v)
{
    auto const n = enum_name(v);
    return n ? *n : std::string_view{"<?>"};
}

// Package a failed check into a report carrying its message.
constexpr ValidationReport failure(ValidationError error, const MsgBuf& m)
{
    ValidationReport r;
    r.ok = false;
    r.error = error;
    r.len = m.len;
    for (std::size_t i = 0; i < m.len; ++i)
    {
        r.text[i] = m.text[i];
    }
    return r;
}

}  // namespace detail

// Validate `Table` against every well-formedness check. Returns {ok=true} for a
// well-formed machine; otherwise {ok=false, error=...} with a message naming the
// offender. Consteval, so callable from both a static_assert and a runtime test.
template <auto Table>
consteval ValidationReport validate()
{
    using State = typename decltype(Table)::State;

    // Check 1: exactly one Top State exists.
    std::size_t tops = 0;
    State firstTop{};
    State secondTop{};
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (Table.states[i].isTop)
        {
            if (tops == 0)
            {
                firstTop = Table.states[i].state;
            }
            else if (tops == 1)
            {
                secondTop = Table.states[i].state;
            }
            ++tops;
        }
    }
    if (tops == 0)
    {
        detail::MsgBuf m;
        m += "no Top State declared (every machine needs exactly one)";
        return detail::failure(ValidationError::NoTopState, m);
    }
    if (tops > 1)
    {
        detail::MsgBuf m;
        m += "multiple Top States: ";
        m += detail::name(firstTop);
        m += " and ";
        m += detail::name(secondTop);
        return detail::failure(ValidationError::MultipleTopStates, m);
    }

    // A State is "declared" when it has a row in the table.
    auto declared = [](State s) {
        for (std::size_t i = 0; i < Table.stateCount; ++i)
        {
            if (Table.states[i].state == s)
            {
                return true;
            }
        }
        return false;
    };

    // Check 2: every non-Top State has a declared parent.
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        if (!Table.states[i].isTop && !declared(Table.states[i].parent))
        {
            detail::MsgBuf m;
            m += "State ";
            m += detail::name(Table.states[i].state);
            m += " has undeclared parent ";
            m += detail::name(Table.states[i].parent);
            return detail::failure(ValidationError::MissingParent, m);
        }
    }

    // `parent` is a Composite State when some other State names it as parent;
    // `child_of` is true when `c` is a declared child of `parent`.
    auto isComposite = [](State parent) {
        for (std::size_t i = 0; i < Table.stateCount; ++i)
        {
            if (Table.states[i].parent == parent && Table.states[i].state != parent)
            {
                return true;
            }
        }
        return false;
    };
    auto childOf = [](State c, State parent) {
        for (std::size_t i = 0; i < Table.stateCount; ++i)
        {
            if (Table.states[i].state == c && Table.states[i].parent == parent)
            {
                return true;
            }
        }
        return false;
    };

    // Check 3: every Composite State has an Initial Substate that is one of its
    // children -- otherwise the machine could not settle in a Leaf beneath it.
    for (std::size_t i = 0; i < Table.stateCount; ++i)
    {
        State const s = Table.states[i].state;
        if (!isComposite(s))
        {
            continue;
        }
        if (!Table.states[i].hasInitial)
        {
            detail::MsgBuf m;
            m += "Composite State ";
            m += detail::name(s);
            m += " has no Initial Substate";
            return detail::failure(ValidationError::InitialNotChild, m);
        }
        if (!childOf(Table.states[i].initial, s))
        {
            detail::MsgBuf m;
            m += "Composite State ";
            m += detail::name(s);
            m += " has Initial Substate ";
            m += detail::name(Table.states[i].initial);
            m += " that is not one of its children";
            return detail::failure(ValidationError::InitialNotChild, m);
        }
    }

    // Check 4: every Transition Target is a declared State.
    using Event = typename decltype(Table)::Event;
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        if (!declared(Table.transitions[i].target))
        {
            detail::MsgBuf m;
            m += "Transition (";
            m += detail::name(Table.transitions[i].source);
            m += ", ";
            m += detail::name<Event>(Table.transitions[i].event);
            m += ") targets undeclared State ";
            m += detail::name(Table.transitions[i].target);
            return detail::failure(ValidationError::TargetNotDeclared, m);
        }
    }

    // `unwired` is true when `s` was opted out of the exhaustiveness check via
    // .unwired (a work-in-progress enumerator with no State row yet).
    auto unwired = [](State s) {
        for (std::size_t i = 0; i < Table.unwiredCount; ++i)
        {
            if (Table.unwiredStates[i] == s)
            {
                return true;
            }
        }
        return false;
    };

    // Check 5: every enumerator of the State enum is wired into the tree -- it has
    // a declared State row, unless it is explicitly marked .unwired.
    for (State s : enum_values<State>())
    {
        if (!declared(s) && !unwired(s))
        {
            detail::MsgBuf m;
            m += "enumerator ";
            m += detail::name(s);
            m += " is not wired into the tree (declare it as a State, or mark it .unwired)";
            return detail::failure(ValidationError::EnumeratorNotWired, m);
        }
    }

    // Check 6: no two Transitions share a (Source, Event) without distinct Guards.
    // A pair on the same Source and Event is ambiguous unless both carry a non-null
    // Guard and the two Guards differ -- the only case where dispatch can pick one.
    for (std::size_t i = 0; i < Table.transitionCount; ++i)
    {
        for (std::size_t j = i + 1; j < Table.transitionCount; ++j)
        {
            auto const& a = Table.transitions[i];
            auto const& b = Table.transitions[j];
            if (a.source != b.source || a.event != b.event)
            {
                continue;
            }
            bool const distinctGuards = a.guard != nullptr && b.guard != nullptr && a.guard != b.guard;
            if (!distinctGuards)
            {
                detail::MsgBuf m;
                m += "duplicate Transition (";
                m += detail::name(a.source);
                m += ", ";
                m += detail::name<Event>(a.event);
                m += ") without distinct Guards";
                return detail::failure(ValidationError::DuplicateTransition, m);
            }
        }
    }

    return ValidationReport{};
}

}  // namespace eta_hsm
