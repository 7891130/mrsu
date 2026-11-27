# KPM 运行时（KernelPatch module）

MRSU 在 KernelSU 之上实现了一套 KernelPatch Module（`.kpm`）运行时，目标是能直接加载
为 kpatch 编译的模块，同时保持 KernelSU 自己的功能不受影响。

> 状态：内核侧 `kernelsu.ko` 与独立运行时 `kpm.ko` 已通过 **8 档 KMI 编译验证**（含 modpost），
> 纯逻辑部分有 **9 套 / 281 项宿主机断言**（CI 每次推送都跑）；**尚未在真机上运行**。
> 本文最后一段是上机自检清单。

## 模块格式

`.kpm` 是 **aarch64 ET_REL** 可重定位 ELF（不是 `.ko`，`finit_module` 加载不了）。
入口靠段名约定，与 kpatch 一致：

| 段 | 作用 |
|---|---|
| `.kpm.init` | 加载时调用，签名 `long (*)(const char *args, const char *event, void *reserved)` |
| `.kpm.exit` | 卸载时调用，`long (*)(void *reserved)` |
| `.kpm.info` | `name=/version=/license=/author=/description=` 字符串 |
| `.kpm.ctl0` / `.kpm.ctl1` | 可选控制入口 |
| `.kpm.event` | 可选，接收运行时事件 |

## 独立运行时：kpm.ko

运行时不一定要长在 `kernelsu.ko` 里。同一套代码还编成 **`kpm.ko`**，由 KernelSU 模块
开机 `insmod`，这样升级 KPM 只需换一个 .ko，不用重刷 boot image。

两个模块共用 `kpm_table.c`（模块表），各自持有一份；**同一时刻只有一个对用户态可见**：

| 模块 | 角色 |
|---|---|
| `kernelsu.ko` | `KSU_IOCTL_KPM` 入口、`kpm` feature 闸门、路由；自带一份模块表作为兜底 |
| `kpm.ko` | 加载后通过 `ksu_kpm_register_ops` 接管控制通道，命令与开机事件直通它的表 |

要点：

- 用户态**不变**：`ksud kpm ...` 仍然只走 `KSU_IOCTL_KPM`，不需要改 CLI 或界面；
- `kpm.ko` 加载时按名字解析 `ksu_kpm_register_ops` / `ksu_kpm_unregister_ops`
  （`find_any_symbol_exact`，它比 `find_kernel_symbol_exact` 多允许模块符号——
  LKM 模式下 `kernelsu.ko` 自己就是模块）；找不到就 **拒绝加载**，
  不会起一个没人能访问的运行时；
- 接管被拒绝（`-EBUSY`）的两种情况：已经有 owner，或 `kernelsu.ko` 的表里已经装了模块。
  两张活表会静默分叉，先前加载的模块会"消失"；
- 路由调用全程持 owner 互斥锁，所以 `kpm.ko` 的 `module_exit` 不会在命令执行到一半时把表拆掉。

```bash
ksud kpm insmod                               # 载入 APK 内置的 kpm.ko（按当前 KMI 选）
insmod /data/adb/modules/<某个模块>/kpm.ko    # 或者自己 insmod 一份
dmesg | grep -i kpm                           # "runtime owns the control channel"
```

管理器里：KPM → **KPM 集成** → 「加载独立运行时」，同一件事；旁边的「诊断」跑的是
`ksud kpm doctor`，第一行 `standalone:` 会告诉你现在是谁在服务控制通道。

## 加载流程

1. `ksud kpm load <file>` → `KSU_IOCTL_KPM`（uapi 7）；
2. 内核校验 ELF 头（ET_REL / AArch64 / 段表边界）；
3. 段布局：text → RO → RW，边界按页对齐（text 页最后置只读）；
4. 用 `module_alloc` 拿可执行内存，拷贝段；
5. 符号简化：未定义符号先查 kfunc 表，再回落 `find_kernel_symbol_exact`（kallsyms，含非导出符号）；
6. AArch64 RELA 重定位（与 `arch/arm64/kernel/module.c` 同源，私有的
   `aarch64_insn_encode_immediate` 实现）；
7. icache 刷新、text 置只读；
8. 调用 `.kpm.init`；失败则调 `.kpm.exit` 并回收。

## 已提供的 kfunc（31 个）

**符号与基础（9）**
`symbol_lookup_name`、`kallsyms_lookup_name`、`compat_copy_to_user`（**返回已拷贝长度**，失败返回 0）、
`compat_strncpy_from_user`、`current_uid`、`kp_symbol_count`、`kp_symbol_list`、
`kp_feature_list`、`kver`（数据符号）。

**inline hook（4）**
`hook`、`unhook`、`fp_hook`、`fp_unhook`。返回值为 kpatch 的 `hook_err_t`
（0 / 4095 / 4094 / 4093 / 4092），不是内核 errno。

**wrap hook（4）**
`hook_wrap`、`hook_unwrap_remove`、`hook_chain_add`、`hook_chain_remove`。
`argno` 最多 8（x0–x7），回调里改 `fargs->args[]` 会生效；`skip_origin` 置 1 可跳过原函数。

**BTF（7）**
`kp_btf_available`、`kp_btf_find_type`、`kp_btf_type_size`、`kp_btf_type_info`、
`kp_btf_member`、`kp_btf_member_offset`、`kp_btf_skip_mod`。无 `CONFIG_DEBUG_INFO_BTF`
时统一返回 `-EOPNOTSUPP`。

**syscall（7）**
`syscalln_addr`、`syscalln_name_addr`、`_task_pt_reg`（只读查询），
`hook_syscalln`、`unhook_syscalln`、`hook_compat_syscalln`、`unhook_compat_syscalln`。
`hook_syscalln` 是"查表拿到 handler 地址 → 对该地址做 wrap hook"，
**不改写 `sys_call_table` 本身**；未找到 compat 表时 compat 版本返回 `-ENOENT`。

**刻意不提供**（模块引用会以 unknown symbol 失败，属预期）：
`hook_prepare`/`hook_install`/`hook_uninstall`（kpatch 的显式 `hook_t` 接口）、
`kp_btf_raw`（内核不向模块暴露 BTF 数据指针）、`kp_kconfig_*`，
以及 APatch 专属的 `su_*`/`kstorage_*`/`ap_mod_*`。
## 用法

```bash
ksud kpm load /data/adb/kpm/foo.kpm          # 临时加载（重启失效）
ksud kpm list                                 # 已加载列表
ksud kpm info <name>                          # 详情
ksud kpm unload <name>                        # 卸载
ksud kpm stage /path/to/foo.kpm               # 放入 /data/adb/kpm，开机自动加载
ksud kpm unstage foo.kpm                      # 取消开机加载
ksud kpm staged                               # 开机清单
ksud kpm autoload                             # 手动触发（post-fs-data 已自动调用）
ksud kpm insmod                               # 载入 APK 内置的独立运行时 kpm.ko
ksud kpm ctl0 <name> <args...>                # 模块自定义控制
ksud kpm doctor                               # 排障：接口/feature/ioctl/目录/standalone/内核日志
```

管理器里：设置 → KernelPatch modules。

## 上机自检清单（M6）

先跑 ksud kpm doctor：它会打印内核版本、UAPI、feature 状态、一次 list ioctl 往返、`/data/adb/kpm` 目录、控制通道由谁提供（`standalone:` 一行）和最近的 kpm 内核日志。接口缺失、feature 关闭、ioctl 不通这三类问题一句话就能定位。

然后按顺序做，每步先看 `dmesg | grep -i kpm`。

1. **模块镜像能否分配可执行内存**
   `kpm: loaded <name> (<version>)` 出现即通过。
   失败 `cannot allocate executable memory` → `module_alloc` 符号未解析或权限不足。

2. **trampoline 距离**（关键，宿主机无法验证）
   在模块里 hook 一个序言含 `b.cond`/`cbz` 的函数。
   若报 `cannot build a hook for this function`（`-ERANGE`），说明
   `module_alloc` 返回的模块区距离内核正文超过 ±1MB。
   对策：换 trampoline 分配策略（目标附近空洞分配，或改用绝对跳转形式）。

3. **文本改写**
   `kpm: hooked 0x… -> 0x… (N-insn patch)` 出现，且目标函数行为确实改变。
   注意：补丁走 `ksu_patch_text`（内部 `stop_machine`），**必须一次调用写完整块**。

4. **卸载还原**
   `ksud kpm unload` 后原函数行为恢复，且不崩。
   （曾有一个逐条恢复的 bug：会留下"原第一条 + 跳转尾巴"，已修）

5. **开机自动加载**
   `/data/adb/kpm/*.kpm` 在 post-fs-data 被加载；安全模式下**不加载**。

6. **失败诊断**
   故意加载一个引用不存在符号的模块，界面/CLI 应显示
   `unknown symbol: xxx`（而不是笼统失败）。

7. **回归**
   `selinux_hide`、`su`、模块挂载等既有功能不受影响；
   内核日志无 `kpm:` 以外的异常。

## 验证状态（更新于 2026-11-26 夜）

诚实记录，避免后来者把测试覆盖误当成设备验证。

### 已验证

宿主套件就住在仓库里（`kernel/kpm/tests/`），CI 的 `hosttests` 作业每次推送都会跑：

```bash
bash kernel/kpm/tests/run.sh
```

| 范围 | 方式 | 结果 |
|---|---|---|
| 重定位（ABS64/PREL/CALL26/ADRP+ADD/溢出/跳过） | 宿主机单测 + 独立解码器 | 11 项 |
| ELF 加载流程（布局/元数据/符号/入口/回滚/释放） | 宿主机集成测试，真实 `kpm_elf_load` | 23 项 |
| AArch64 编码/解码/PC 相对重定位 | 与 NDK clang + ld.lld 输出逐条比对 | 46 项 |
| hook 规划（短/长跳转、序言搬迁、拒绝路径） | 宿主机单测 | 48 项 |
| hook 注册表 | 宿主机单测 | 21 项 |
| wrap 帧 ABI 偏移 + 回调链 | 宿主机单测 | 27 项 |
| 模块表（加载/卸载/list/info/ctl/事件/失败路径）**+ 控制通道归属与转发** | 宿主机单测 | 56 项 |
| kfunc 返回约定 | 宿主机单测 | 20 项 |
| `kpm.ko` 入口（通道解析、接管、拒绝、卸载清表） | 宿主机单测 | 29 项 |
| 补丁是否跨页 | 宿主机单测 | 6 项 |
| ksud CLI（全部 kpm 子命令） | 本地 `cargo check` / `cargo build --release` | 通过 |
| 管理器界面（屏幕/路由/入口/加载动作） | 本地 `assembleRelease` 出签名 APK | 通过 |

合计 **9 套 / 281 项宿主机断言**，在 CI 的 Linux 上全绿；另有 8 档 KMI 的
`kernelsu.ko` + `kpm.ko` 编译，以及 CLI/管理器的本地构建。


### 审查发现并已修复的缺陷

四个，都是编译器抓不到的：

1. **卸载时的 `stop_machine` 竞态** —— 逐条恢复会让线程执行「原第一条 + 残留跳转尾巴」；
2. **跨页补丁写越界** —— `ksu_patch_text` 只映射一页；该规则已抽成纯函数并有 6 项测试；
3. **11 个文件缺 `<linux/errno.h>`** —— 用 `-EINVAL` 却不包含定义它的头，CI 必挂；
4. **`module_alloc` 在 6.10 被 `execmem_alloc` 取代** —— android16-6.12 / android17-6.18
   会运行期失效，而编译与 CI 都不报错（见上文符号可用性表）。

### 尚未验证

- **真机运行**：trampoline 距离、文本改写、cache 刷新、真实 `.kpm` 的符号解析
  全部只有推理，没有实测。按上面的自检清单走。
- **`kpm.ko` 的真实接管**：`ksu_kpm_register_ops` 的解析在 LKM 模式下走的是
  "允许模块符号"的查找路径，宿主机只能测到"解析失败就拒绝加载"这一半；
  真机要确认的是 `dmesg` 里出现 `runtime owns the control channel`。

（此前"内核从未编译"的说法已作废：仓库转为公开后 Actions 分钟数免费，
8 档编译已跑通，见下。）

### 编译验证记录

| 运行 | 提交 | 结果 |
|---|---|---|
| [45744806351](https://github.com/7891130/mrsu/actions/runs/45744806351) | `32c63771` | 8/8 失败——`kpm_symbol.c` 第 8 行含字面量 `` `n ``，`kpm_kfunc_hook.h` 未被包含，后续 `kpm_kf_*` 全部未声明 |
| [45745293073](https://github.com/7891130/mrsu/actions/runs/45745293073) | `60b2477e` | **8/8 通过** |
| [45864659294](https://github.com/7891130/mrsu/actions/runs/45864659294) | `1e342fb7` | 8 档 `kernelsu.ko` + `kpm.ko` 全部通过；`hosttests` 红（套件首次在 Linux 上编译，见下） |
| [45866023821](https://github.com/7891130/mrsu/actions/runs/45866023821) | `94a2087f` | **8/8 通过 + 9 套 281 项宿主机断言全绿** |
| [45867426425](https://github.com/7891130/mrsu/actions/runs/45867426425) | `c56f055f` | 同上，且在 `kernel/kpm` 跑过 clang-format 之后（`ClangFormat check` 同提交转为绿）|

覆盖：android12-5.10、android13-5.10、android13-5.15、android14-5.15、
android14-6.1、android15-6.6、android16-6.12、android17-6.18。

两个错误值得记一笔：

1. `kpm_symbol.c` 里的反引号转义序列是 PowerShell 替换留下的，所有人工审查与
   宿主机测试都没发现——只有真正的编译器会发现。
2. 宿主机套件搬进仓库后第一次在 Linux 上编译，**9 套全挂**：`stubs/linux/errno.h`
   原本 `#include <errno.h>`，而 glibc 的 `<errno.h>` 反过来向 `<linux/errno.h>` 要
   错误码，`-I stubs` 让这个 include 指回自己，于是所有 `E*` 一个都没定义。
   MinGW 的 `<errno.h>` 是自带的，所以这条路径以前从未被走到。
### 已知局限

- 未提供 `hook_prepare`/`hook_install`/`hook_uninstall` 与 `kp_btf_raw`，模块引用会加载失败（见上）；

- trampoline 必须与目标同在 ±1MB 内（被顶掉的 `b.cond`/`cbz` 位移限制），
  否则 `kpm_hook_build()` 返回 `-ERANGE`；
- `kpm_notify_event()` 已实现但尚未接入 KSU 启动事件；
- 按名字解析内核符号的设计要求每新增符号都核对版本可用性表。

