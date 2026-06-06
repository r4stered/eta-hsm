#!/usr/bin/env bash
# Codegen gate for the README's event-path performance claims.
#
# Compiles the codegen-probe TU at -O2 (the README's reference shape) and reads
# two structural properties off the disassembly of the whole object, failing the
# build on regression -- the codegen counterpart to footprint_test.cpp's
# compile-time static_asserts:
#
#   1. Zero heap allocation on the event path: no call to any allocation symbol
#      (operator new/delete, malloc/free, _Znwm/_Znam, ...). A symbol-name match,
#      so it is architecture-independent and stable across toolchain bumps.
#   2. No function-pointer table walk: no indirect call/branch through a register.
#      The transition table is a constexpr non-type template argument, so the
#      compiler resolves every pointer-to-member Action/Guard to a direct (often
#      inlined) call; a surviving indirect call would mean that broke. Matched
#      per architecture (aarch64 blr/br; x86-64 call */jmp *).
#
# dispatch is emitted out-of-line in its own section and the wrapper tail-calls
# it, so the gate disassembles the whole object, not a single symbol.
#
# The gate is self-validating: it also compiles a planted variant whose Action
# escapes a `new`, and asserts the no-heap matcher CATCHES it -- so a vacuous
# pass (the matcher silently matching nothing) fails the gate too.
#
# Run from the repository root, inside the GCC-16 toolchain:
#
#   ./docker_build bash tools/codegen_gate.sh
#
# The compiler and objdump default to the toolchain's but honor $CXX / $OBJDUMP.

set -euo pipefail

readonly CXX="${CXX:-g++-16}"
readonly OBJDUMP="${OBJDUMP:-objdump}"
readonly SRC="eta_hsm/probe/codegen_probe_main.cpp"
readonly FLAGS=(-std=c++26 -freflection -fcontracts -O2 -I .)

# Allocation symbols any heap use on the event path would call, demangled or not.
readonly ALLOC_RE='operator new|operator delete|_Znw[mj]|_Zna[mj]|_Zd[la]P|\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b'

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

# Disassemble the whole object for one build of the probe (extra args appended).
# -r interleaves relocations: a call to an external symbol (operator new, the
# contract handler, ...) names its target as a relocation on x86-64, where the
# plain disassembly leaves it unresolved -- aarch64 annotates the target inline,
# x86-64 does not, so the relocation is what makes the symbol-name match portable.
disasm() {  # <out-tag> [extra compiler args...]
    local tag="$1"
    shift
    "${CXX}" "${FLAGS[@]}" "$@" -c "${SRC}" -o "${work}/${tag}.o"
    "${OBJDUMP}" -dCr "${work}/${tag}.o"
}

# Print any indirect call/branch instruction (aarch64 or x86-64). Reads the
# disassembly on stdin; anchors on the mnemonic field so symbol names that merely
# contain "br"/"call" do not false-match. Exits 0 if it printed at least one.
indirect_calls() {
    awk -F'\t' '
        /:\t/ {
            instr = $3
            if (NF >= 4 && $4 != "") instr = instr " " $4
            gsub(/[ \t]+/, " ", instr)
            # aarch64: blr/br <reg> are register-indirect (b/bl are not matched).
            if (instr ~ /^(blr|br) /) { print; hit = 1; next }
            # x86-64 AT&T: an indirect call/jmp has a `*` operand.
            if (instr ~ /^(call|callq|jmp|jmpq) +\*/) { print; hit = 1 }
        }
        END { exit(hit ? 0 : 1) }
    '
}

# --- The real probe: must be clean on both properties. -----------------------
asm="$(disasm clean)"

if grep -Eq "${ALLOC_RE}" <<<"${asm}"; then
    echo "FAIL: an allocation symbol appears on the event path:"
    grep -E "${ALLOC_RE}" <<<"${asm}" | sed 's/^/  /'
    exit 1
fi

if indirect_out="$(indirect_calls <<<"${asm}")"; then
    echo "FAIL: an indirect call/branch survives in dispatch (function-pointer table walk):"
    sed 's/^/  /' <<<"${indirect_out}"
    exit 1
fi

# --- Self-test: a planted heap allocation MUST trip the no-heap matcher. ------
planted_asm="$(disasm planted -DCODEGEN_PROBE_PLANT_HEAP)"
if ! grep -Eq "${ALLOC_RE}" <<<"${planted_asm}"; then
    echo "FAIL: planted heap allocation was NOT caught -- the no-heap matcher is vacuous."
    exit 1
fi

echo "OK: dispatch is heap-free and has no function-pointer table walk; planted regression is caught."
