#!/usr/bin/env bash
# Build-failure assertion for the compile-time validator (issue 0006).
#
# Compiles eta_hsm/tests/validator_negcompile.cpp for one VALIDATOR_CASE and
# succeeds ONLY IF the compile fails AND the compiler output matches the expected
# offender pattern. This is how a negative case ("this machine must not compile")
# becomes an ordinary passing test: ctest runs this script per case.
#
#   expect_compile_fail.sh <case-number> <expected-egrep-pattern>
#
# Run from the repository root (where the eta_hsm/ include prefix resolves). The
# compiler defaults to g++-16 but honors $CXX.

set -u

readonly CASE="${1:?usage: expect_compile_fail.sh <case-number> <expected-pattern>}"
readonly PATTERN="${2:?usage: expect_compile_fail.sh <case-number> <expected-pattern>}"
readonly CXX="${CXX:-g++-16}"
readonly SRC="eta_hsm/tests/validator_negcompile.cpp"

# -fsyntax-only is enough: the validator's static_assert fires while the Machine
# specialization is instantiated, before any code generation.
output="$("${CXX}" -std=c++26 -freflection -fsyntax-only -I . \
    "-DVALIDATOR_CASE=${CASE}" "${SRC}" 2>&1)"
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
