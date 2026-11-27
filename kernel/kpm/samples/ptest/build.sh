#!/usr/bin/env bash
# Build ptest.kpm -- a freestanding aarch64 ET_REL object, i.e. a `.kpm`.
#
#   ./build.sh [output-dir]        (needs an NDK; CC selects the compiler)
#
# A .kpm is not a kernel module: it is never handed to finit_module(). The KPM
# loader in the kernel parses it, lays out its sections, applies AArch64
# relocations and jumps to the pointers in the .kpm.* sections, so the object
# must stay position-independent-ish, carry no unwind tables and pull in no
# libc: everything below is aimed at that.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:-$here/out}"

CC="${CC:-clang}"
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "no compiler: set CC=/path/to/clang (an NDK r26+ clang works)" >&2
    exit 1
fi

mkdir -p "$out"

"$CC" \
    --target=aarch64-linux-gnu \
    -c -O2 -std=gnu11 \
    -ffreestanding -nostdlib -fno-builtin \
    -fno-stack-protector -fno-pic \
    -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -mgeneral-regs-only -mno-outline-atomics \
    -Wall -Wextra \
    -o "$out/ptest.kpm" "$here/ptest.c"

echo "built: $out/ptest.kpm"
if command -v llvm-readelf >/dev/null 2>&1; then
    llvm-readelf -h "$out/ptest.kpm" | sed -n '1,12p'
    llvm-readelf -S "$out/ptest.kpm" | grep -E '\.kpm\.|\.text' || true
    echo "undefined symbols (must be none):"
    llvm-readelf -s --wide "$out/ptest.kpm" | awk '$7 == "UND" && $8 != "" {print "  " $8}'
fi
