#!/usr/bin/env bash
# Capacity-override gate for the builder/table cap knob.
#
# Proves three things about ETA_HSM_MAX_STATES (eta_hsm/machine/hsm.hpp), each by
# compiling eta_hsm/tests/cap_override.cpp -- a star machine of exactly
# `kMaxStates + CAP_OVER` declared States -- under a different cap:
#
#   1. At the default cap (64), a machine exactly at capacity compiles AND runs,
#      and one State over is rejected with "too many States (limit 64)".
#   2. Raising the cap with -DETA_HSM_MAX_STATES lets a machine past the default
#      ceiling compile and run (the knob really raises the limit).
#   3. The over-capacity diagnostic interpolates whatever cap is in force, not a
#      literal -- so it names "limit <raised>" under a raised cap.
#
# A pure ctest case with no build target: the script drives the compiler itself, so
# it runs wherever the GCC-16 toolchain lives. Run from the repository root, inside
# the toolchain container:
#
#   ./docker_build bash tools/cap_override.sh
#
# The compiler defaults to g++-16 but honors $CXX.

set -uo pipefail

readonly CXX="${CXX:-g++-16}"
readonly SRC="eta_hsm/tests/cap_override.cpp"
readonly FLAGS=(-std=c++26 -freflection -fcontracts -fcontract-evaluation-semantic=enforce -I .)
readonly RAISED=128

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

fail() {
    echo "FAIL: $*"
    exit 1
}

# Compile + run cap_override.cpp; the machine must build cleanly and its binary
# must exit 0. Extra args (e.g. -DETA_HSM_MAX_STATES / -DCAP_OVER) are appended.
expect_compiles_and_runs() {  # <tag> <description> [extra args...]
    local tag="$1" desc="$2"
    shift 2
    local bin="${work}/${tag}"
    if ! "${CXX}" "${FLAGS[@]}" "$@" "${SRC}" -o "${bin}" 2>"${work}/${tag}.err"; then
        echo "----- compiler output -----"
        sed 's/^/  /' "${work}/${tag}.err"
        fail "${desc}: expected a clean compile, but it was rejected."
    fi
    if ! "${bin}" >"${work}/${tag}.out" 2>&1; then
        echo "----- program output -----"
        sed 's/^/  /' "${work}/${tag}.out"
        fail "${desc}: compiled but the binary exited nonzero."
    fi
    echo "OK: ${desc} -- $(cat "${work}/${tag}.out")"
}

# Compile cap_override.cpp expecting REJECTION whose diagnostic matches a pattern.
expect_rejected_with() {  # <tag> <description> <pattern> [extra args...]
    local tag="$1" desc="$2" pattern="$3"
    shift 3
    local output status
    output="$("${CXX}" "${FLAGS[@]}" -fsyntax-only "$@" "${SRC}" 2>&1)"
    status=$?
    if [[ ${status} -eq 0 ]]; then
        fail "${desc}: compiled cleanly but was expected to be rejected."
    fi
    if ! grep -Eq "${pattern}" <<<"${output}"; then
        echo "----- compiler output -----"
        sed 's/^/  /' <<<"${output}"
        fail "${desc}: rejected, but the diagnostic did not match: ${pattern}"
    fi
    echo "OK: ${desc} -- rejected with a diagnostic matching: ${pattern}"
}

# 1a. Default cap, exactly at capacity (64 States): compiles and runs.
expect_compiles_and_runs default_at_cap \
    "default cap, machine exactly at capacity (64 States)" -DCAP_OVER=0

# 1b. Default cap, one State over: rejected, diagnostic names the live cap (64).
expect_rejected_with default_over_cap \
    "default cap, machine one State over (65)" \
    "too many States \(limit 64\)" -DCAP_OVER=1

# 2. Raised cap: a machine past the default ceiling now compiles and runs.
expect_compiles_and_runs raised_at_cap \
    "raised cap ${RAISED}, machine of ${RAISED} States (past the default 64)" \
    "-DETA_HSM_MAX_STATES=${RAISED}" -DCAP_OVER=0

# 3. Raised cap, one State over: the diagnostic tracks the raised cap, not 64.
expect_rejected_with raised_over_cap \
    "raised cap ${RAISED}, machine one State over ($((RAISED + 1)))" \
    "too many States \(limit ${RAISED}\)" \
    "-DETA_HSM_MAX_STATES=${RAISED}" -DCAP_OVER=1

echo
echo "OK: the cap knob raises the State ceiling and the capacity diagnostic tracks the live cap."
