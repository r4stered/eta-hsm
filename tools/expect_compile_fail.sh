#!/usr/bin/env bash
# Build-failure assertion for a deliberately ill-formed translation unit.
#
# Compiles one negative case of a "must not compile" TU and succeeds ONLY IF the
# compile fails AND the compiler output matches the expected offender pattern.
# This is how a negative case becomes an ordinary passing test: ctest runs this
# script per case. It backs two harnesses today -- the compile-time validator
# (issue 0006, validator_negcompile.cpp / -DVALIDATOR_CASE) and the run_hook
# hook-detection contract (issue 0005 follow-up, run_hook_negcompile.cpp /
# -DRUNHOOK_CASE) -- so it takes the TU and its case macro as optional arguments.
#
#   expect_compile_fail.sh <case-number> <expected-egrep-pattern> [src] [macro]
#
# <src> defaults to the validator TU and <macro> to VALIDATOR_CASE, so the
# original two-argument validator invocations keep working unchanged.
#
# Run from the repository root (where the eta_hsm/ include prefix resolves). The
# compiler defaults to g++-16 but honors $CXX.

set -u

readonly CASE="${1:?usage: expect_compile_fail.sh <case-number> <expected-pattern> [src] [macro]}"
readonly PATTERN="${2:?usage: expect_compile_fail.sh <case-number> <expected-pattern> [src] [macro]}"
readonly SRC="${3:-eta_hsm/tests/validator_negcompile.cpp}"
readonly MACRO="${4:-VALIDATOR_CASE}"
readonly CXX="${CXX:-g++-16}"

# -fsyntax-only is enough: the offending construct (the validator's static_assert,
# or run_hook's spliced call) is reached while the Machine specialization is
# instantiated, before any code generation.
output="$("${CXX}" -std=c++26 -freflection -fsyntax-only -I . \
    "-D${MACRO}=${CASE}" "${SRC}" 2>&1)"
status=$?

if [[ ${status} -eq 0 ]]; then
    echo "FAIL: case ${CASE} compiled cleanly but was expected to be rejected."
    exit 1
fi

if ! grep -Eq "${PATTERN}" <<<"${output}"; then
    echo "FAIL: case ${CASE} was rejected, but the diagnostic did not name the offender."
    echo "  expected pattern: ${PATTERN}"
    echo "----- compiler output -----"
    echo "${output}"
    exit 1
fi

echo "OK: case ${CASE} rejected with a diagnostic matching: ${PATTERN}"
exit 0
