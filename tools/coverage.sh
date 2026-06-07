#!/usr/bin/env bash
# Line-coverage harness for the GTest suite.
#
# Compiles the suite under GCC's --coverage instrumentation (the `coverage`
# CMakePreset: -fprofile-arcs -ftest-coverage, debug/-O0), runs it under ctest to
# emit the .gcda profile data, then reduces the .gcda/.gcno with gcovr into an
# overall line-coverage percentage. The library is header-only, so this measures
# how much of the headers the suite exercises.
#
# All scratch lands under the git-ignored build/coverage/ tree:
#   build/coverage/                     the instrumented build + .gcda/.gcno data
#   build/coverage/report/coverage.html         human-readable HTML report
#   build/coverage/report/coverage.cobertura.xml  machine-readable report (CI)
#
# Repeatable: re-running reconfigures and rebuilds from scratch. Run it in the
# toolchain container from the repo root (the only supported toolchain):
#
#   ./docker_build bash tools/coverage.sh
#
# gcovr drives gcov; both must match the compiler that produced the data, so it
# defaults to gcov-16 (override with $GCOV).

set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
readonly BUILD_DIR="${REPO_ROOT}/build/coverage"
readonly REPORT_DIR="${BUILD_DIR}/report"
readonly GCOV="${GCOV:-gcov-16}"

# Configure -> build -> run. `cmake --preset` resolves CMakePresets.json from the
# repo root; the preset roots the build under build/coverage/.
cd "${REPO_ROOT}"
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage

# Reduce the profile data. Root at the repo so paths render as eta_hsm/...; keep
# only the shipped library surface -- the production headers under machine/,
# reflect/, utils/, log/, and diagram/. The test sources, the differential-test
# support (reference/, probe/), the demo machines (examples/), the GTest
# dependency under build/, and the standalone tool entry points (*_main.cpp,
# which ctest never runs) are all left out so the headline % tracks the library
# the suite exercises, not the harness around it.
mkdir -p "${REPORT_DIR}"
cd "${REPO_ROOT}"
gcovr \
    --root "${REPO_ROOT}" \
    --gcov-executable "${GCOV}" \
    --filter 'eta_hsm/machine/' \
    --filter 'eta_hsm/reflect/' \
    --filter 'eta_hsm/utils/' \
    --filter 'eta_hsm/log/' \
    --filter 'eta_hsm/diagram/' \
    --exclude '.*_main\.cpp$' \
    --html-details "${REPORT_DIR}/coverage.html" \
    --cobertura "${REPORT_DIR}/coverage.cobertura.xml" \
    --cobertura-pretty \
    --print-summary \
    "${BUILD_DIR}"
