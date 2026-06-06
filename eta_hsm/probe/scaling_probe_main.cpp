// Single translation unit the scaling-probe harness compiles once per (N, Depth)
// point. It instantiates `Machine<generate<N, Depth>()>` and drives
// it through a full dispatch so the heavy parts -- the validator, the P1306
// dispatch expansion over every Transition, and the reflection-detected hooks --
// are all instantiated and code-generated. The harness recompiles this file with
// -DPROBE_N=… -DPROBE_DEPTH=… under -ftime-report and reads off the compile time
// and peak memory; see tools/scaling_probe.sh.
//
// This is not a test (it has no assertions about behavior -- scaling_probe_test
// covers correctness). Its only job is to be a realistic, fully-instantiated
// machine of a given size for the toolchain to chew on.

#include "eta_hsm/machine/machine.hpp"
#include "eta_hsm/probe/scaling_generator.hpp"
#include "eta_hsm/reflect/enum_reflection.hpp"

#ifndef PROBE_N
#define PROBE_N 7
#endif
#ifndef PROBE_DEPTH
#define PROBE_DEPTH 1
#endif

namespace {

using eta_hsm::enum_values;
using eta_hsm::Machine;
using eta_hsm::probe::generate;
using eta_hsm::probe::ProbeEvent;

// The probe machine for this compile. Built as a namespace-scope constant so the
// table (and its validation) is instantiated even before main runs.
constexpr auto kTable = generate<PROBE_N, PROBE_DEPTH>();

}  // namespace

int main()
{
    Machine<kTable> machine;

    // Dispatch every Event and run a During tick, forcing the full dispatch and
    // hook expansion to be generated. A volatile sink keeps the optimizer from
    // discarding the machine under -O2.
    for (ProbeEvent event : enum_values<ProbeEvent>())
    {
        machine.dispatch(event);
    }
    machine.during();

    static volatile int sink = 0;
    sink = static_cast<int>(machine.identify());
    return sink;
}
