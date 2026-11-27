# ptest — a KPM smoke test

`ptest.kpm` is the smallest module that still exercises every part of the
runtime. It is meant to be the **first** thing loaded on a fresh device: it
depends on *no* external symbols, so if it fails to load, the problem is the
loader itself (ELF parsing, section layout, relocations, entry binding) and not
a missing kfunc or kernel symbol. Once it loads, anything that still fails is in
a much smaller search space.

| Section | Function | Does |
|---|---|---|
| `.kpm.init` | `ptest_init(args, event, reserved)` | counts the call, records `strlen(args)`, returns 0 |
| `.kpm.exit` | `ptest_exit(reserved)` | counts the call, returns 0 |
| `.kpm.ctl0` | `ptest_ctl0(ctl_args, out, outlen)` | returns `strlen(ctl_args)` |
| `.kpm.ctl1` | `ptest_ctl1(a1, a2, a3)` | returns `a1 + a2 + a3` |
| `.kpm.event` | `ptest_event(event, args, reserved)` | counts the call, returns 0 |
| `.kpm.info` | — | `name=ptest version=0.1 license=GPL author=MRSU …` |

It deliberately does not write into the `ctl0` output buffer: that needs
`copy_to_user`, i.e. an external symbol, which is exactly what this module
avoids on purpose.

## Build

```bash
./build.sh                    # CC=/path/to/ndk/clang, needs the aarch64 target
```

`ptest.kpm` in this directory is the checked-in result, so the host suite
(`kernel/kpm/tests/test_sample.c`) can load a compiler-produced image in CI
instead of only the hand-built ones. Rebuild and commit it whenever `ptest.c`
changes.

## What the host test can and cannot do

`test_sample.c` runs the real `kpm_elf_load()` over this image and checks the
parsed metadata plus the entry slots (each must be relocated to an address
inside the loaded image). It **cannot call** the entries: they are AArch64
machine code and the harness runs on the host — an earlier version of that test
did try, and died with SIGSEGV, which is a property of the harness and not of
the module. Calling them is what the device does, and what the `dmesg` lines
below are for.

## Use on device

```bash
ksud kpm load /data/adb/kpm/ptest.kpm     # or: manager -> KPM -> Import .kpm
ksud kpm list                             # ptest  0.1
ksud kpm info ptest                       # has_ctl0=1 has_ctl1=1 has_event=1
ksud kpm ctl0 ptest hello                 # returns 5 (the length of "hello")
ksud kpm ctl1 ptest 1 2 3                 # prints 6
ksud kpm unload ptest                     # exit is called
```

What to look for in `dmesg | grep -i kpm`:

- `kpm: loaded ptest (0.1)` — layout, relocations and init all worked;
- `kpm: calling exit of ptest` — unload ran the exit entry.

If instead you get `unknown symbol:` this module was modified to reference
something new; as committed it has none (check with `llvm-readelf -s`).

To stage it for every boot: `ksud kpm stage /data/adb/kpm/ptest.kpm`, then it is
loaded from `/data/adb/kpm` during post-fs-data.
