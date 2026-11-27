# Host-side unit tests for the KPM runtime.
#
# Compiles the real kernel sources (`kernel/kpm/*.c`) against the stub kernel
# headers in stubs/, so the loader, the hook engine and the control-channel
# routing can be validated without a device, WSL or a kernel tree.
#
#   bash kernel/kpm/tests/run.sh          # CI and any POSIX host
#   powershell -File kernel/kpm/tests/run.ps1   # Windows host with gcc in PATH
#
# Caveat for MinGW hosts: they are LLP64 (`unsigned long` is 32-bit) while the
# kernel is LP64.
#   - stubs/linux/kernel.h pins BIT() to 64 bits;
#   - the loader converts pointers via uintptr_t/u64 and uses u64 for symbol
#     addresses, so nothing truncates.

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$stub = Join-Path $here 'stubs'
$repo = (Resolve-Path (Join-Path $here '..\..\..')).Path
$kdir = Join-Path $repo 'kernel'
$out = if ($env:HOSTTEST_OUT) { $env:HOSTTEST_OUT } else { Join-Path $here 'build' }

New-Item -ItemType Directory -Force -Path $out | Out-Null

$common = @('-std=gnu11', '-Wall', '-I', $stub, '-I', $kdir, '-I', $repo, '-D__aarch64__', '-DKSU_NEW_DCACHE_FLUSH=1')
$failed = 0

function Invoke-Suite {
    param([string]$Label, [string]$Test, [string[]]$Sources)

    Write-Host ''
    Write-Host "== $Label"
    $exe = Join-Path $out ([System.IO.Path]::GetFileNameWithoutExtension($Test))
    & gcc @common -o $exe (Join-Path $here $Test) @Sources
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $Test" }
    & $exe
    if ($LASTEXITCODE -ne 0) { $script:failed = 1 }
}

$kpm = Join-Path $kdir 'kpm'

Invoke-Suite -Label '[1/10] relocation'       -Test 'test_reloc.c'      -Sources @(Join-Path $kpm 'kpm_reloc.c')
Invoke-Suite -Label '[2/10] elf loader'       -Test 'test_elf.c'        -Sources @((Join-Path $kpm 'kpm_elf.c'), (Join-Path $kpm 'kpm_reloc.c'))
Invoke-Suite -Label '[3/10] instructions'     -Test 'test_insn.c'       -Sources @(Join-Path $kpm 'kpm_insn.c')
Invoke-Suite -Label '[4/10] hook planner'     -Test 'test_hook.c'       -Sources @((Join-Path $kpm 'kpm_hook.c'), (Join-Path $kpm 'kpm_insn.c'))
Invoke-Suite -Label '[5/10] hook registry'    -Test 'test_hook_table.c' -Sources @(Join-Path $kpm 'kpm_hook_table.c')
Invoke-Suite -Label '[6/10] hook frames'      -Test 'test_hook_fargs.c' -Sources @(Join-Path $kpm 'kpm_hook_chain.c')
Invoke-Suite -Label '[7/10] module table + owner routing' -Test 'test_core.c' -Sources @((Join-Path $here 'host_fs.c'), (Join-Path $kpm 'kpm_core.c'), (Join-Path $kpm 'kpm_table.c'))
Invoke-Suite -Label '[8/10] kfunc layer'      -Test 'test_kfunc.c'      -Sources @(Join-Path $kpm 'kpm_kfunc.c')
Invoke-Suite -Label '[9/10] standalone runtime' -Test 'test_standalone.c' -Sources @((Join-Path $here 'host_fs.c'), (Join-Path $kpm 'kpm_standalone.c'), (Join-Path $kpm 'kpm_core.c'), (Join-Path $kpm 'kpm_table.c'))

Write-Host ''
Write-Host '== [10/10] sample module'
$sampleExe = Join-Path $out 'test_sample'
& gcc @common -o $sampleExe (Join-Path $here 'test_sample.c')
if ($LASTEXITCODE -ne 0) { throw 'sample module test failed to compile' }
& $sampleExe (Join-Path $here '..\samples\ptest\ptest.kpm')
if ($LASTEXITCODE -ne 0) { $failed = 1 }

Write-Host ''
if ($failed -ne 0) { Write-Host 'SOME SUITES FAILED'; exit 1 }
Write-Host 'ALL SUITES PASSED'
exit 0
