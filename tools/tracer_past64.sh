#!/usr/bin/env bash
# Tracer bullet: one machine compiled and differentially verified past the default cap.
#
# Proves the entire past-64 pipeline end-to-end on a single machine before the full
# four-shape sweep (tools/scaling_frontier.sh) widens it. It:
#
#   1. Builds the shape emitter (eta_hsm/probe/scaling_frontier_emit_main.cpp).
#   2. Emits a balanced tree of N (default 128) States as a standalone .cpp -- the
#      State and Event enumerators spelled out as source.
#   3. Compiles it under the GCC-16 toolchain with the cap raised
#      (-DETA_HSM_MAX_STATES), instantiating a real Machine<Table> so the full
#      reflection codegen and the consteval validator actually run on a machine far
#      past the default 64-State ceiling.
#   4. Runs it: the exhaustive backbone drives every reachable (State, Event) pair,
#      diffing production dispatch against the independent reference and asserting
#      the universal invariants. Any divergence exits nonzero and fails the run.
#
# Green means a machine far past the default cap compiles, instantiates the real
# `template for` dispatch, and rests/transitions exactly as the reference says -- at
# scale. The verified-pair count is printed (no silent truncation).
#
# Run from the repository root, inside the GCC-16 toolchain:
#
#   ./docker_build bash tools/tracer_past64.sh
#
# The compiler defaults to g++-16 but honors $CXX. Override N / the raised cap with
# the TRACER_N / TRACER_CAP env vars.

set -euo pipefail

readonly CXX="${CXX:-g++-16}"
readonly EMIT_SRC="eta_hsm/probe/scaling_frontier_emit_main.cpp"
readonly N="${TRACER_N:-128}"
readonly CAP="${TRACER_CAP:-256}"

# Soft limits cranked generously so the true subject (does it compile and run past
# 64) is what is measured, not GCC's default constexpr/template budgets.
readonly FLAGS=(
    -std=c++26 -freflection -fcontracts -fcontract-evaluation-semantic=enforce -I .
    -ftemplate-depth=4096 -fconstexpr-depth=4096 -fconstexpr-ops-limit=1000000000
    "-DETA_HSM_MAX_STATES=${CAP}" "-DETA_HSM_MAX_TRANSITIONS=4096"
)

work="$(mktemp -d)"
keep=0
cleanup() { [ "${keep}" = 1 ] || rm -rf "${work}"; }
trap cleanup EXIT

readonly emit="${work}/emit"
echo "Building the shape emitter (${EMIT_SRC})..."
"${CXX}" -std=c++26 -freflection -fcontracts -fcontract-evaluation-semantic=enforce -I . -O0 "${EMIT_SRC}" -o "${emit}"

readonly src="${work}/tracer.cpp"
readonly bin="${work}/tracer"
echo "Emitting a balanced tree of N=${N} States..."
"${emit}" balanced_tree "${N}" >"${src}"

echo "Compiling it with the cap raised to ${CAP} (instantiating a real Machine<Table>)..."
if ! "${CXX}" "${FLAGS[@]}" -O0 "${src}" -o "${bin}" 2>"${work}/cc.err"; then
    keep=1
    echo "FAIL: the past-64 machine did not COMPILE:" >&2
    sed 's/^/    /' "${work}/cc.err" >&2
    echo "  source kept at: ${src}" >&2
    exit 1
fi

echo "Running it through the exhaustive backbone differential..."
if ! out="$("${bin}" 2>&1)"; then
    keep=1
    echo "FAIL: the past-64 machine diverged from the reference:" >&2
    sed 's/^/    /' <<<"${out}" >&2
    echo "  source kept at: ${src}" >&2
    exit 1
fi

echo "  ${out}"
echo
echo "OK: a balanced tree of N=${N} States (past the default 64-State cap) compiled, instantiated a real"
echo "  Machine<Table>, and matched the reference on every reachable (State, Event) pair."
