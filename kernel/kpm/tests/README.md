# KPM host-side tests

Compiles the **real** sources in `kernel/kpm/` against the stub kernel headers in
`stubs/` and runs them on the build host. No device, no WSL, no kernel tree.

```bash
bash kernel/kpm/tests/run.sh            # CI and any POSIX host with gcc
powershell -File kernel/kpm/tests/run.ps1   # Windows host with gcc in PATH
```

CI runs `run.sh` in the `hosttests` job of `.github/workflows/kpm-compile.yml`.

| Suite | Covers |
|---|---|
| `test_reloc` | AArch64 RELA application, overflow rejection, skip rules |
| `test_elf` | full `.kpm` load path: layout, metadata, symbols, entries, rollback |
| `test_insn` | instruction encode/decode, PC-relative retargeting |
| `test_hook` | hook planning: patch form, prologue relocation, refusals |
| `test_hook_table` | hook registry |
| `test_hook_fargs` | wrap-frame offsets and the callback chain |
| `test_core` | module table lifecycle **and** which owner serves a command |
| `test_kfunc` | kfunc return conventions the modules rely on |
| `test_standalone` | `kpm.ko` entry point: channel lookup, handover, teardown |

## What this does not prove

The tests exercise logic, not the kernel. Text patching (`stop_machine`,
fixmaps), trampoline distance, cache maintenance and real `.kpm` modules can
only be verified on a device -- see `docs/KPM.md` for that checklist. Compiling
for the real kernel is covered separately by the 8-KMI matrix in
`kpm-compile.yml`.

## Caveats

- **MinGW is LLP64** (`unsigned long` is 32-bit) while the kernel is LP64.
  `stubs/linux/kernel.h` pins `BIT()` to 64 bits and the loader converts
  pointers through `uintptr_t`/`u64`, so nothing truncates.
- The stubs are deliberately dumb: no scheduling, no concurrency, locks are
  no-ops. Anything that needs two threads to go wrong is out of scope here.
- `test_core` and `test_standalone` link `kpm_table.c` once, so the built-in
  table and `kpm.ko`'s table are the same object. What is verified is *routing*
  and the handover rules, not that each module gets a private table.
