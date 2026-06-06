#!/bin/bash
# Run the CI lint gates locally with the exact versions the pipeline pins
# (see .github/workflows/linux.yml). These are formatters/linters, not the
# compiler, so they run on the host — not in the GCC-16 toolchain image.
#
# The pinned tools are cached in a git-ignored venv (tools/.lintenv) created on
# the first run and reused after, so repeat runs do no install work. The venv is
# rebuilt automatically only when the pinned versions below change.
#
#   ./tools/lint.sh        # run all gates; non-zero exit == would fail CI
#   ./tools/lint.sh --fix  # reformat files in place instead of checking
set -euo pipefail

FIX=0
case "${1:-}" in
  --fix) FIX=1 ;;
  "") ;;
  *) echo "usage: $0 [--fix]" >&2; exit 2 ;;
esac

# Keep the venv fully isolated: a sourced ROS/conda env leaks site-packages in via
# PYTHONPATH and lets the wrong deps shadow the pinned linters.
unset PYTHONPATH

readonly ROOT="$(cd "$(dirname "$0")/.." && pwd)"
readonly VENV="${ROOT}/tools/.lintenv"
readonly STAMP="${VENV}/.pins"
readonly PINS="gersemi==0.27.7 clang-format==21.*"

if [[ ! -f "${STAMP}" || "$(cat "${STAMP}" 2>/dev/null)" != "${PINS}" ]]; then
  echo "==> Setting up lint venv (one-time; pins changed or first run)…"
  rm -rf "${VENV}"
  python3 -m venv "${VENV}"
  "${VENV}/bin/pip" install -q --upgrade pip
  # shellcheck disable=SC2086
  "${VENV}/bin/pip" install -q ${PINS}
  echo "${PINS}" >"${STAMP}"
fi

readonly BIN="${VENV}/bin"
cd "${ROOT}"

if [[ "${FIX}" -eq 1 ]]; then
  echo "==> cmake_format  (gersemi, in place)"
  "${BIN}/gersemi" --in-place eta_hsm tests/consumer

  echo "==> cpp_linting   (clang-format 21, in place)"
  find eta_hsm \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
    | xargs -0 "${BIN}/clang-format" -i

  echo "==> all files reformatted"
else
  echo "==> cmake_format  (gersemi)"
  "${BIN}/gersemi" --check eta_hsm tests/consumer

  echo "==> cpp_linting   (clang-format 21)"
  find eta_hsm \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
    | xargs -0 "${BIN}/clang-format" --dry-run --Werror

  echo "==> all lint gates passed"
fi
