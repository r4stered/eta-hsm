#!/usr/bin/env bash
# Codegen fuzzing build loop (csmith-style).
#
# The in-tree codegen sweep (random_codegen_test.cpp) instantiates a fixed corpus
# of random machines as `Machine<Table>` inside one translation unit, so it is
# bounded by a single TU's compile budget. This loop lifts that ceiling: it emits
# each random machine as a *standalone* C++ source file
# (eta_hsm::reference::recipeToSource), compiles it as its own translation unit,
# and runs it -- so an arbitrarily large (and CI-rotatable) seed range can fuzz the
# production `template for` dispatch as separate compilations, past what one TU can
# hold. Each compiled machine drives the exhaustive backbone, diffing production
# dispatch against the independent reference and asserting the universal invariants;
# any divergence makes that machine's binary exit nonzero and fails the run.
#
# Faithfulness: every machine is compiled with -DFUZZ_SELFCHECK, so before the
# backbone runs it re-derives its own machine independently from the seed and
# asserts the spelled-out table matches -- an emitter bug that wires a valid-but-
# wrong machine is caught here, not silently fuzzed. The loop self-tests this safety
# net with a planted valid-but-wrong machine that MUST trip it, so a green run is
# never vacuous.
#
# Reproducibility: the seed range is deterministic given its start, so any failure
# reproduces from the printed seed alone. CI can rotate FUZZ_SEED_START (e.g. to the
# run number) for fresh-each-run novelty without losing reproducibility.
#
# Run from the repository root, inside the GCC-16 toolchain:
#
#   ./docker_build bash tools/codegen_fuzz.sh
#
# The compiler defaults to g++-16 but honors $CXX. Override the sweep with the
# FUZZ_SEED_START / FUZZ_COUNT / FUZZ_NMIN / FUZZ_NMAX / FUZZ_OPT env vars.

set -euo pipefail

readonly CXX="${CXX:-g++-16}"
readonly FLAGS=(-std=c++26 -freflection -fcontracts -fcontract-evaluation-semantic=enforce -I .)
readonly EMIT_SRC="eta_hsm/probe/codegen_fuzz_emit_main.cpp"

# Sweep config (env-overridable). N varies per seed across [NMIN, NMAX]; each seed
# names exactly one machine, so the range is a reproducible, not random, corpus.
readonly SEED_START="${FUZZ_SEED_START:-0}"
readonly COUNT="${FUZZ_COUNT:-24}"
readonly NMIN="${FUZZ_NMIN:-2}"
readonly NMAX="${FUZZ_NMAX:-24}"
readonly OPT="${FUZZ_OPT:-O0}"

work="$(mktemp -d)"
keep=0
cleanup() { [ "${keep}" = 1 ] || rm -rf "${work}"; }
trap cleanup EXIT

readonly emit="${work}/emit"
echo "Building the machine emitter (${EMIT_SRC})..."
"${CXX}" "${FLAGS[@]}" "-${OPT}" "${EMIT_SRC}" -o "${emit}"

# Compile one emitted machine as its own TU (always with the faithfulness check)
# and run it. Returns nonzero, with the source kept and a reproduce line printed, if
# the machine fails to compile or its binary exits nonzero (divergence, invariant
# violation, a tripped self-check, or a vacuous run).
fuzz_machine() {  # <seed> <states> -> echoes the binary's OK line on success
    local seed="$1" n="$2"
    local src="${work}/machine_${seed}.cpp" bin="${work}/machine_${seed}"
    "${emit}" "${seed}" "${n}" >"${src}"
    if ! "${CXX}" "${FLAGS[@]}" "-${OPT}" -DFUZZ_SELFCHECK "${src}" -o "${bin}" 2>"${work}/cc.err"; then
        keep=1
        echo "FAIL: machine seed=${seed} states=${n} did not COMPILE:" >&2
        sed 's/^/    /' "${work}/cc.err" >&2
        echo "  source kept at: ${src}" >&2
        return 1
    fi
    local out
    if ! out="$("${bin}" 2>&1)"; then
        keep=1
        echo "FAIL: machine seed=${seed} states=${n} diverged / self-check tripped:" >&2
        sed 's/^/    /' <<<"${out}" >&2
        echo "  source kept at: ${src}" >&2
        echo "  reproduce: ${emit} ${seed} ${n} > m.cpp && ${CXX} ${FLAGS[*]} -${OPT} -DFUZZ_SELFCHECK m.cpp -o m && ./m" >&2
        return 1
    fi
    echo "${out}"
}

# --- Self-test: a planted valid-but-wrong machine MUST trip the loop. ----------
# Flip the first External Transition (.on) to Local (.local): the result is still a
# well-formed machine (it compiles), but no longer the recipe's machine, so only the
# faithfulness self-check can catch it -- exactly the safety net the sweep relies on.
# A loop that silently swallowed nonzero exits would pass this planted machine, so
# catching it proves the green sweep below is not vacuous.
echo "Self-test: a planted valid-but-wrong machine must trip the faithfulness check..."
planted="${work}/planted.cpp"
"${emit}" 0 6 >"${planted}"
sed -i '0,/m = m\.on(/s//m = m.local(/' "${planted}"
"${CXX}" "${FLAGS[@]}" "-${OPT}" -DFUZZ_SELFCHECK "${planted}" -o "${work}/planted"
if "${work}/planted" >/dev/null 2>&1; then
    keep=1
    echo "FAIL: the planted valid-but-wrong machine was NOT caught -- the fuzz loop is vacuous." >&2
    exit 1
fi
echo "  caught -- the faithfulness check bites, so the sweep below is non-vacuous."
echo

readonly span=$(( NMAX - NMIN + 1 ))
readonly seed_end=$(( SEED_START + COUNT - 1 ))
echo "Fuzzing production codegen over ${COUNT} random machines as separate compilations:"
echo "  seeds        [${SEED_START}, ${seed_end}]"
echo "  states/seed  N = ${NMIN} + (seed % ${span})  -> in [${NMIN}, ${NMAX}]"
echo "  compile      ${CXX} -${OPT}, one translation unit per machine, with -DFUZZ_SELFCHECK"
echo

machines=0
pairs_total=0
n_lo="${NMAX}"
n_hi="${NMIN}"
for (( seed = SEED_START; seed <= seed_end; ++seed )); do
    n=$(( NMIN + (seed % span) ))
    out="$(fuzz_machine "${seed}" "${n}")"  # aborts the run (set -e) on any failure
    pairs="${out##*pairs=}"
    pairs_total=$(( pairs_total + pairs ))
    (( n < n_lo )) && n_lo="${n}"
    (( n > n_hi )) && n_hi="${n}"
    machines=$(( machines + 1 ))
    printf '  seed %-5d states %-3d -> %s\n' "${seed}" "${n}" "${out}"
done

echo
echo "OK: fuzzed ${machines} distinct production machines as ${machines} separate compilations."
echo "  seeds [${SEED_START}, ${seed_end}], states actually spanned [${n_lo}, ${n_hi}],"
echo "  ${pairs_total} reachable (Leaf, Event) pairs verified -- production template-for dispatch"
echo "  diffed against the reference on every machine, each first checked faithful to its recipe."
echo
echo "  Bounds (no silent truncation): ${machines} machines this run (FUZZ_COUNT=${COUNT}),"
echo "  N in [${NMIN}, ${NMAX}] (FUZZ_NMIN/FUZZ_NMAX). Raise FUZZ_COUNT or rotate FUZZ_SEED_START"
echo "  to fuzz a larger / fresh seed range -- the corpus is bounded only by wall-clock here,"
echo "  not by one translation unit's compile budget."
