#!/usr/bin/env bash
# Host-side unit tests for the KPM runtime.
#
# Compiles the real kernel sources (`kernel/kpm/*.c`) against the stub kernel
# headers in stubs/, so the loader, the hook engine and the control-channel
# routing can be validated without a device, WSL or a kernel tree.
#
#   bash kernel/kpm/tests/run.sh
#
# The harness is meant for an LP64 host gcc, like the one CI has. Under MinGW
# (LLP64, `unsigned long` == 32 bit) the stubs keep BIT() 64 bit wide and the
# kernel code converts pointers through uintptr_t/u64, so those runs work too.
#
# Exit status is 0 only when every suite passes.

set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
stub="$here/stubs"
repo="$(cd "$here/../../.." && pwd)"
kdir="$repo/kernel"
out="${HOSTTEST_OUT:-$here/build}"

mkdir -p "$out" || exit 1

common=(-std=gnu11 -Wall -I "$stub" -I "$kdir" -I "$repo" -D__aarch64__ -DKSU_NEW_DCACHE_FLUSH=1)

failed=0

suite() {
    label="$1"
    test="$2"
    shift 2

    echo
    echo "== $label"
    exe="$out/${test%.c}"
    if ! gcc "${common[@]}" -o "$exe" "$here/$test" "$@"; then
        echo "compile failed: $test" >&2
        failed=1
        return
    fi
    if ! "$exe"; then
        failed=1
    fi
}

suite '[1/10] relocation'             test_reloc.c       "$kdir/kpm/kpm_reloc.c"
suite '[2/10] elf loader'             test_elf.c         "$kdir/kpm/kpm_elf.c" "$kdir/kpm/kpm_reloc.c"
suite '[3/10] instructions'           test_insn.c        "$kdir/kpm/kpm_insn.c"
suite '[4/10] hook planner'           test_hook.c        "$kdir/kpm/kpm_hook.c" "$kdir/kpm/kpm_insn.c"
suite '[5/10] hook registry'          test_hook_table.c  "$kdir/kpm/kpm_hook_table.c"
suite '[6/10] hook frames'            test_hook_fargs.c  "$kdir/kpm/kpm_hook_chain.c"
suite '[7/10] module table + owner routing' test_core.c  "$here/host_fs.c" "$kdir/kpm/kpm_core.c" "$kdir/kpm/kpm_table.c"
suite '[8/10] kfunc layer'            test_kfunc.c       "$kdir/kpm/kpm_kfunc.c"
suite '[9/10] standalone runtime'    test_standalone.c  "$here/host_fs.c" "$kdir/kpm/kpm_standalone.c" "$kdir/kpm/kpm_core.c" "$kdir/kpm/kpm_table.c"
# Validates the committed sample module (samples/ptest) from the loader
# side. It cannot be *loaded* here: loading runs its AArch64 init on the host.
# the suite covers a compiler-produced .kpm and not only hand-built images.
echo
echo "== [10/10] sample module"
sampleExe="$out/test_sample"
if ! gcc "${common[@]}" -o "$sampleExe" "$here/test_sample.c"; then
    echo "compile failed: test_sample.c" >&2
    failed=1
else
    # The sample is committed prebuilt (see samples/ptest/README.md).
    if ! "$sampleExe" "$here/../samples/ptest/ptest.kpm"; then
        failed=1
    fi
fi
echo
if [ "$failed" -ne 0 ]; then
    echo 'SOME SUITES FAILED'
    exit 1
fi
echo 'ALL SUITES PASSED'
exit 0
