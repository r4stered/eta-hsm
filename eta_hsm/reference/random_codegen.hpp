#pragma once

// The codegen-fuzzing bridge: materialize a RandomRecipe (random_machine.hpp) into
// a compile-time *production* `Hsm` table, so a random machine can be instantiated
// as `Machine<Table>` and driven through the exhaustive backbone (backbone.hpp).
//
// The runtime reference interpreter walks a TableView and never touches the
// production `template for` dispatch; feeding the same recipe into the real `Hsm{}`
// builder here yields a genuine NTTP table, so `runBackbone<randomTable<Seed, N>()>()`
// fuzzes the production codegen on machine shapes nobody would hand-write -- the only
// path that exercises the real dispatcher on arbitrary topologies.
//
// The recipe is built at compile time from a seed (makeRecipe is constexpr), so a
// seed alone names one production machine: `Machine<randomTable<Seed, N>()>`.

#include <cstddef>
#include <cstdint>

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/probe/scaling_generator.hpp"
#include "eta_hsm/reference/random_machine.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

namespace eta_hsm::reference {

// Feed a recipe into the production `Hsm{}` builder, producing the same compile-time
// table type the hand-written and scaling-probe machines use. Top is declared via
// .initial (the builder's idiom for the isTop root); the remaining States attach via
// .state; Composite States get their Initial Substate; Transitions become .on/.local
// rows (the probe Host has no Actions or Guards); enumerators past the recipe's State
// count are .unwired so the validator's exhaustiveness check still passes.
consteval auto recipeToTable(const RandomRecipe& r)
{
    constexpr auto kStates = eta_hsm::enum_values<ProbeState>();
    constexpr auto kEvents = eta_hsm::enum_values<ProbeEvent>();

    Hsm<ProbeHost, ProbeState, ProbeEvent> m{};

    // Top (the isTop root) and its Initial Substate. A recipe always declares node 0
    // as Top with node 1 attached beneath it, so Top is Composite and carries an
    // Initial Substate.
    m = m.initial(kStates[r.states[0].index], kStates[r.states[0].initial]);

    // Attach every non-Top State under its parent.
    for (std::size_t i = 1; i < r.states.size(); ++i)
    {
        m = m.state(kStates[r.states[i].index], kStates[r.states[i].parent]);
    }

    // Give every non-Top Composite State its Initial Substate (Top is done above).
    for (std::size_t i = 1; i < r.states.size(); ++i)
    {
        if (r.states[i].hasInitial)
        {
            m = m.initial(kStates[r.states[i].index], kStates[r.states[i].initial]);
        }
    }

    // Each Transition becomes a production row. internal is always false in a recipe;
    // local selects the External (.on) vs Local (.local) dispatch variant.
    for (auto const& t : r.transitions)
    {
        if (t.local)
        {
            m = m.local(kStates[t.source], kEvents[t.event], kStates[t.target]);
        }
        else
        {
            m = m.on(kStates[t.source], kEvents[t.event], kStates[t.target]);
        }
    }

    // Opt the enumerators the recipe never wired out of the exhaustiveness check.
    for (std::size_t i = r.states.size(); i < kStates.size(); ++i)
    {
        m = m.unwired(kStates[i]);
    }

    return m;
}

// The production table for the random machine named by `Seed` and `N` States. A
// constant expression usable as `Machine<randomTable<Seed, N>()>`.
template <std::uint64_t Seed, std::size_t N>
consteval auto randomTable()
{
    return recipeToTable(makeRecipe(Seed, N));
}

}  // namespace eta_hsm::reference
