#!/usr/bin/env bash
# Four-shape scaling-frontier sweep: build cost, cliff, correctness, and structural
# runtime scaling, all from one re-runnable harness.
#
# Across four machine shapes -- each isolating a different subsystem -- it pushes the
# State count geometrically upward (64 -> 128 -> 256 -> ...) to bracket the first
# failure, recording for every (shape, N):
#
#   * compile wall-time and peak compiler memory (GCC -ftime-report wall + GGC),
#   * the failure mode at the wall, classified: a DEFAULT soft-limit wall (where a
#     naive user's stock -fconstexpr-ops-limit / -ftemplate-depth budget gives out)
#     vs the HARD ceiling reached with those soft limits cranked generously
#     (OOM / ICE / timeout), and
#   * correctness at scale: the emitted machine is run through the exhaustive backbone
#     differential (production dispatch == independent reference, invariants hold);
#     any divergence ABORTS the run naming the offending (shape, N), and
#   * structural runtime scaling: max Exit/Entry steps per dispatch and the deepest
#     active path[] -- bounded-work properties, no wall-clock runtime numbers.
#
# The four shapes (eta_hsm/probe/shape_source.hpp):
#   deep_chain         depth ~ N-1   -> per-dispatch chain length, LCA, path depth
#   wide_star          depth 1       -> `template for` breadth, table size
#   balanced_tree      depth ~ log N -> the realistic middle
#   dense_transitions  wide Events   -> the validator's O(transitions^2) scan
#
# Honesty: the sweep range, the per-shape N actually spanned, the cliff N and its
# mode, and the verified-pair counts are all logged -- no silent truncation. A
# compile that intentionally OOMs / ICEs at the frontier is recorded data, not a
# harness error. The cap knob (ETA_HSM_MAX_STATES / _TRANSITIONS) is sized exactly to
# each machine so the table storage never inflates another shape's measurement.
#
# Output (under docs/, gitignored per project convention):
#   docs/probes/scaling-frontier.csv   raw per-(shape, N) measurements
#   docs/probes/scaling-frontier.md    the report
#
# Run from the repository root, inside the GCC-16 toolchain:
#
#   ./docker_build bash tools/scaling_frontier.sh
#
# The compiler defaults to g++-16 but honors $CXX. Override the sweep with the
# FRONTIER_NS / FRONTIER_TIMEOUT / FRONTIER_OPT env vars.

# Not -e: compiles are EXPECTED to fail at the cliff; we classify and continue.
set -uo pipefail

readonly CXX="${CXX:-g++-16}"
readonly EMIT_SRC="eta_hsm/probe/scaling_frontier_emit_main.cpp"
readonly OUT_DIR="docs/probes"
readonly CSV="${OUT_DIR}/scaling-frontier.csv"
readonly REPORT="${OUT_DIR}/scaling-frontier.md"

readonly SHAPES=(deep_chain wide_star balanced_tree dense_transitions worst_case)
read -r -a NS <<<"${FRONTIER_NS:-64 128 256}"
readonly OPT="${FRONTIER_OPT:-O0}"
readonly TIMEOUT="${FRONTIER_TIMEOUT:-180}"  # per-compile wall ceiling (seconds)
readonly RUN_TIMEOUT="${FRONTIER_RUN_TIMEOUT:-120}"  # per-run wall ceiling

# Soft limits cranked generously, so the cranked probe reaches the true HARD ceiling
# (memory / ICE / time) rather than stopping at GCC's stock budgets.
readonly CRANK=(
    -ftemplate-depth=100000
    -fconstexpr-depth=100000
    -fconstexpr-loop-limit=2000000000
    -fconstexpr-ops-limit=2000000000000
)
readonly BASE=(-std=c++26 -freflection -fcontracts -fcontract-evaluation-semantic=enforce -I . "-${OPT}")

mkdir -p "${OUT_DIR}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

readonly emit="${work}/emit"
echo "Building the shape emitter (${EMIT_SRC})..."
"${CXX}" "${BASE[@]}" "${EMIT_SRC}" -o "${emit}" || { echo "FAIL: emitter did not build." >&2; exit 1; }

# States/Transitions a (shape, N) machine declares -- mirrors shape_source.hpp, so the
# per-TU caps are sized exactly to the machine and never inflate the table storage.
state_count() { echo "$2"; }
trans_count() {  # <shape> <n>
    local shape="$1" n="$2"
    # dense_transitions and worst_case both transition every non-Top State on every
    # Event (eventCount == n), so the table holds (n-1)*n rows; the others are sparse.
    if [ "${shape}" = "dense_transitions" ] || [ "${shape}" = "worst_case" ]; then
        echo $(( (n - 1) * n ))
    else
        echo $(( (n - 1) * 3 + 1 ))
    fi
}

# Classify a compile outcome from its exit status and captured output.
classify() {  # <status> <output-file>
    local status="$1" out="$2"
    if [ "${status}" = 124 ]; then echo TIMEOUT; return; fi
    if [ "${status}" = 0 ]; then echo OK; return; fi
    if grep -qiE 'internal compiler error|please submit a full bug report' "${out}"; then echo ICE; return; fi
    if [ "${status}" = 137 ] || grep -qiE 'out of memory|memory exhausted|cannot allocate|bad_alloc|Killed' "${out}"; then
        echo OOM; return
    fi
    if grep -qiE 'constexpr-ops-limit|constexpr loop|template instantiation depth|constexpr-depth|operations limit|exceeds maximum' "${out}"; then
        echo SOFT_LIMIT; return
    fi
    echo ERROR
}

# Globals set by compile_probe: PROBE_STATUS PROBE_MODE PROBE_WALL PROBE_GGC.
compile_probe() {  # <src> <out> <cap_states> <cap_trans> [extra flags...]
    local src="$1" out="$2" caps="$3" capt="$4"
    shift 4
    local outf="${work}/cc.out"
    timeout "${TIMEOUT}" "${CXX}" "${BASE[@]}" \
        "-DETA_HSM_MAX_STATES=${caps}" "-DETA_HSM_MAX_TRANSITIONS=${capt}" \
        "$@" -ftime-report "$src" -o "$out" >"${outf}" 2>&1
    PROBE_STATUS=$?
    PROBE_MODE="$(classify "${PROBE_STATUS}" "${outf}")"
    PROBE_WALL="n/a"
    PROBE_GGC="n/a"
    if [ "${PROBE_STATUS}" = 0 ]; then
        # The -ftime-report TOTAL line's last two fields are wall seconds and GGC peak.
        local total
        total="$(grep -E '^ TOTAL' "${outf}" | tail -1)"
        if [ -n "${total}" ]; then
            PROBE_WALL="$(awk '{print $(NF-1)}' <<<"${total}")"
            PROBE_GGC="$(awk '{
                m=$NF; u=m; sub(/[0-9.]+/,"",u); v=m; sub(/[a-zA-Z]+$/,"",v)
                if (u=="G") mb=v*1024; else if (u=="M") mb=v; else if (u=="k") mb=v/1024; else mb=v/(1024*1024)
                printf "%.1f", mb }' <<<"${total}")"
        fi
    fi
    PROBE_OUT="${outf}"
}

echo "shape,n,states,transitions,hard_mode,wall_s,ggc_mb,default_soft_mode,reachable_leaves,events,pairs,max_steps,max_depth" >"${CSV}"

echo
echo "Sweeping shapes [${SHAPES[*]}] over N=[${NS[*]}] (timeout ${TIMEOUT}s/compile) with ${CXX}..."
echo

aborted=0
declare -A SOFT_WALL SOFT_MODE CLIFF_N CLIFF_MODE SPANNED

for shape in "${SHAPES[@]}"; do
    echo "== ${shape} =="
    SOFT_WALL[$shape]="none in [${NS[*]}]"
    SOFT_MODE[$shape]="-"
    CLIFF_N[$shape]="none in [${NS[*]}]"
    CLIFF_MODE[$shape]="-"
    SPANNED[$shape]=""
    soft_walled=0

    for n in "${NS[@]}"; do
        caps="$(state_count "${shape}" "${n}")"
        capt="$(trans_count "${shape}" "${n}")"
        src="${work}/${shape}_${n}.cpp"
        "${emit}" "${shape}" "${n}" >"${src}"

        # Default-soft-limit probe: stock GCC budgets, cap raised so the cap is not the
        # limiter. Records where a naive user would have been stopped. Compile-only.
        compile_probe "${src}" "${work}/soft.o" "${caps}" "${capt}" -c
        local_soft_mode="${PROBE_MODE}"
        if [ "${soft_walled}" = 0 ] && [ "${PROBE_STATUS}" != 0 ]; then
            SOFT_WALL[$shape]="${n}"
            SOFT_MODE[$shape]="${local_soft_mode}"
            soft_walled=1
        fi

        # Hard-ceiling probe: soft limits cranked, compiled + linked so it can run.
        bin="${work}/${shape}_${n}"
        compile_probe "${src}" "${bin}" "${caps}" "${capt}" "${CRANK[@]}"
        hard_mode="${PROBE_MODE}"
        wall="${PROBE_WALL}"
        ggc="${PROBE_GGC}"

        if [ "${hard_mode}" != OK ]; then
            CLIFF_N[$shape]="${n}"
            CLIFF_MODE[$shape]="${hard_mode}"
            printf '  N=%-5d compile %-10s wall=%-6s ggc=%-7s  default-soft=%s -> CLIFF\n' \
                "${n}" "${hard_mode}" "${wall}" "${ggc}" "${local_soft_mode}"
            echo "${shape},${n},${caps},${capt},${hard_mode},${wall},${ggc},${local_soft_mode},,,,," >>"${CSV}"
            break  # stop doubling this shape -- the wall is bracketed
        fi

        # Cranked compile succeeded: run the machine through the backbone differential.
        if ! run_out="$(timeout "${RUN_TIMEOUT}" "${bin}" 2>&1)"; then
            echo "  N=${n} compiled but DIVERGED at runtime:" >&2
            sed 's/^/      /' <<<"${run_out}" >&2
            echo >&2
            echo "ABORT: ${shape} at N=${n} -- production dispatch diverged from the reference or an invariant failed." >&2
            cp "${src}" "${OUT_DIR}/frontier-divergence-${shape}-${n}.cpp" 2>/dev/null || true
            aborted=1
            break
        fi

        # Parse the OK line's structural counts: "... reachableLeaves=R events=E pairs=P maxSteps=S maxDepth=D".
        get() { sed -nE "s/.*$1=([0-9]+).*/\1/p" <<<"${run_out}"; }
        rl="$(get reachableLeaves)"; ev="$(get events)"; pairs="$(get pairs)"
        ms="$(get maxSteps)"; md="$(get maxDepth)"
        SPANNED[$shape]="${SPANNED[$shape]} ${n}"
        printf '  N=%-5d compile OK         wall=%-6s ggc=%-7s  pairs=%-7s maxSteps=%-4s maxDepth=%-4s default-soft=%s\n' \
            "${n}" "${wall}" "${ggc}" "${pairs}" "${ms}" "${md}" "${local_soft_mode}"
        echo "${shape},${n},${caps},${capt},OK,${wall},${ggc},${local_soft_mode},${rl},${ev},${pairs},${ms},${md}" >>"${CSV}"
    done

    [ "${aborted}" = 1 ] && break
    echo "  spanned N:${SPANNED[$shape]:- none}  |  default-soft wall: N=${SOFT_WALL[$shape]} (${SOFT_MODE[$shape]})  |  hard cliff: N=${CLIFF_N[$shape]} (${CLIFF_MODE[$shape]})"
    echo
done

# --------------------------------------------------------------------------
# Render the report.
# --------------------------------------------------------------------------
{
    echo "# Scaling-frontier sweep"
    echo
    echo "_Generated by \`tools/scaling_frontier.sh\`. Re-run to refresh._"
    echo
    echo "Four machine shapes from \`generate\`-style emission (\`eta_hsm/probe/shape_source.hpp\`),"
    echo "each compiled under GCC 16 / \`-std=c++26 -freflection\` with the capacity cap sized to the"
    echo "machine, then run through the exhaustive backbone differential. Memory is peak GC"
    echo "allocation (GGC) from \`-ftime-report\` -- the project's compile-memory proxy."
    echo
    echo "Sweep range: N in [${NS[*]}] (geometric). Per-compile timeout ${TIMEOUT}s."
    echo
    echo "## Per-shape frontier"
    echo
    echo "| Shape | N spanned | Default-soft wall | Hard cliff |"
    echo "| ----- | --------- | ----------------- | ---------- |"
    for shape in "${SHAPES[@]}"; do
        printf "| %s | %s | N=%s (%s) | N=%s (%s) |\n" \
            "${shape}" "${SPANNED[$shape]:- none}" \
            "${SOFT_WALL[$shape]}" "${SOFT_MODE[$shape]}" \
            "${CLIFF_N[$shape]}" "${CLIFF_MODE[$shape]}"
    done
    echo
    echo "## Per-(shape, N) measurements"
    echo
    echo "| Shape | N | States | Transitions | Compile | Wall (s) | GGC (MB) | Pairs | maxSteps | maxDepth |"
    echo "| ----- | -: | -----: | ----------: | ------- | -------: | -------: | ----: | -------: | -------: |"
    awk -F, 'NR>1 {printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", $1,$2,$3,$4,$5,$6,$7,$11,$12,$13}' "${CSV}"
    echo
    if [ "${aborted}" = 1 ]; then
        echo "**ABORTED on a correctness divergence** -- see the offending TU in \`${OUT_DIR}/\`."
    else
        echo "No correctness divergence: at every spanned N the production dispatch matched the"
        echo "reference on every reachable (State, Event) pair and the universal invariants held."
    fi
    echo
    echo "_No silent truncation: every shape's spanned N, default-soft wall, hard cliff, and verified-pair count is recorded above._"
} >"${REPORT}"

echo
echo "Wrote ${CSV} and ${REPORT}."
if [ "${aborted}" = 1 ]; then
    exit 1
fi
echo "OK: four-shape frontier swept; cliffs and modes classified; correctness held at every spanned N."
