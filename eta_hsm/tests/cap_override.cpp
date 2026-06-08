// Capacity-override harness for the builder/table cap knob.
//
// Builds a star machine of exactly `kMaxStates + CAP_OVER` declared States (one
// Top plus the rest as direct children), then instantiates Machine<table> -- the
// site where the validator's capacity static_assert fires. With CAP_OVER == 0 the
// table sits exactly at capacity and compiles + runs; with CAP_OVER == 1 it is one
// over and the build must fail with the named capacity diagnostic carrying the
// *live* limit.
//
// tools/cap_override.sh drives this TU under several -DETA_HSM_MAX_STATES settings
// to prove the knob raises the ceiling (a machine past the default 64 compiles only
// under a raised cap) and that the diagnostic tracks whatever cap is in force. The
// State enum carries a single enumerator and the States are cast values, so the
// reflection cost stays trivial and this stays a pure capacity check, independent of
// the wide-enum past-64 emitter.

#include <cstddef>
#include <iostream>

#include "eta_hsm/machine/hsm.hpp"
#include "eta_hsm/machine/machine.hpp"

namespace {

using namespace eta_hsm;

struct H {};
enum class Big { Top };
enum class E { Go };

#ifndef CAP_OVER
#define CAP_OVER 0
#endif

// Top plus (kMaxStates - 1 + CAP_OVER) leaf children, so stateCount is exactly
// kMaxStates + CAP_OVER. The builder drops writes past capacity but keeps the
// count, so an over-cap table is representable and the validator catches it.
consteval auto makeStarTable()
{
    Hsm<H, Big, E> t = Hsm<H, Big, E>{}.initial(Big::Top, static_cast<Big>(1));
    std::size_t const children = kMaxStates - 1 + CAP_OVER;
    for (std::size_t i = 1; i <= children; ++i)
    {
        t = t.state(static_cast<Big>(i), Big::Top);
    }
    return t;
}
inline constexpr auto table = makeStarTable();

}  // namespace

int main()
{
    Machine<table> m;
    m.dispatch(E::Go);
    std::cout << "OK cap=" << kMaxStates << " states=" << table.stateCount << " leaf=" << static_cast<int>(m.identify())
              << "\n";
    return 0;
}
