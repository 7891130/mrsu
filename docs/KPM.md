# KPM 运行时（KernelPatch module）

MRSU 在 KernelSU 之上实现了一套 KernelPatch Module（`.kpm`）运行时，目标是能直接加载
为 kpatch 编译的模块，同时保持 KernelSU 自己的功能不受影响。

> 状态：代码完整、纯逻辑部分有 229 项宿主机单元测试；**内核侧尚未经过编译器与真机验证**。
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

## 已提供的 kfunc（16 个）

`symbol_lookup_name`、`kallsyms_lookup_name`、`compat_copy_to_user`（**返回已拷贝长度**，失败返回 0）、
`compat_strncpy_from_user`、`current_uid`、`kp_symbol_count`、`kp_symbol_list`、
`kp_feature_list`、`kver`（数据符号）、`hook`、`unhook`、`fp_hook`、`fp_unhook`。

`hook()` 的返回值是 kpatch 的 `hook_err_t`（0 / 4095 / 4094 / 4093 / 4092），
不是内核 errno。

没实现（模块引用会以 unknown symbol 失败，属预期）：`hook_wrap`/`hook_unwrap`/
`hook_chain_*`、`syscall*` 系列、`kp_btf_*`/`kp_kconfig_*`，以及 APatch 专属的
`su_*`/`kstorage_*`/`ap_mod_*`。

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
ksud kpm ctl0 <name> <args...>                # 模块自定义控制
ksud kpm doctor                               # 排障：接口/feature/ioctl/目录/内核日志一次看全
```

管理器里：设置 → KernelPatch modules。

## 上机自检清单（M6）

先跑 ksud kpm doctor：它会打印内核版本、UAPI、feature 状态、一次 list ioctl 往返、/data/adb/kpm 目录和最近的 kpm 内核日志。接口缺失、feature 关闭、ioctl 不通这三类问题一句话就能定位。

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

## 验证状态（写于 2026-11-26）

诚实记录，避免后来者误判这套代码的可信度。

### 已验证

| 范围 | 方式 | 结果 |
|---|---|---|
| 重定位（ABS64/PREL/CALL26/ADRP+ADD/溢出/跳过） | 宿主机单测，独立解码器校验 | 13 项通过 |
| ELF 加载流程（布局/元数据/符号/入口/回滚/释放） | 宿主机集成测试，真实 `kpm_elf_load` | 21 项通过 |
| AArch64 编码/解码/PC 相对重定位 | 与 NDK clang + ld.lld 的输出逐条比对 | 45 项通过 |
| hook 规划（短/长跳转、序言搬迁、拒绝路径） | 宿主机单测 | 39 项通过 |
| hook 注册表 | 宿主机单测 | 21 项通过 |
| wrap 帧 ABI 偏移 + 回调链 | 宿主机单测 | 30 项通过 |
| 模块表（加载/卸载/list/info/ctl/事件/失败路径） | 宿主机单测 | 39 项通过 |
| kfunc 返回约定 | 宿主机单测 | 21 项通过 |
| ksud CLI（全部 kpm 子命令） | 本地 `cargo check` / `cargo build --release` | 通过 |
| 管理器界面（屏幕/路由/入口/加载动作） | 本地 `assembleRelease` 出签名 APK | 通过 |

合计 **229 项宿主机断言** + CLI/管理器本地构建。

### 未验证（重要）

- **内核代码从未被编译器编译过。** GitHub Actions 分钟数耗尽、WSL 未启用，
  8 档 KMI 编译验证欠 13 个提交。
- **从未在真机上运行过。** trampoline 距离、文本改写、cache 刷新、
  真实 `.kpm` 的符号解析全部只有推理，没有实测。
- 已通过人工审查发现并修复两个编译器抓不到的缺陷：
  卸载时逐条恢复导致的 `stop_machine` 竞态、跨页补丁写越界。

### 已知局限

- 未实现 `hook_wrap`/`hook_chain_*`/`syscall*`/`kp_btf_*`，模块引用会加载失败；
- trampoline 必须落在目标 ±1MB 内（被顶掉的 `b.cond`/`cbz` 位移限制），
  否则 `kpm_hook_build()` 返回 `-ERANGE`；
- `kpm_notify_event()` 尚未接入 KSU 的启动事件。

## 运行期解析符号的版本可用性

本运行时不通过导出符号表链接内核函数，而是用 `find_kernel_symbol_exact()` 按名字从
kallsyms 里找。好处是能调用非导出函数，代价是**编译器完全不参与检查**——名字不存在
只会在运行期暴露，且只出现在缺它的那几档 KMI 上。

因此每新增一个解析符号，都必须先核对它在目标内核上的存在性：

| 符号 | 5.10 | 6.12 | 备注 |
|---|---|---|---|
| `module_alloc` | 有 | **无** | 6.10 起移除 |
| `module_memfree` | 有 | **无** | 同上 |
| `execmem_alloc` | 无 | 有 | 6.10 起取代 module_alloc |
| `execmem_free` | 无 | 有 | 同上 |
| `set_memory_ro` | 有 | 有 | 签名 `(unsigned long, int)` 未变 |
| `set_memory_rw` / `set_memory_x` | 有 | 有 | 备用 |
| `vmalloc` / `vfree` | 有 | 有 | 标准导出 |

`kpm_alloc_exec()` 因此先探 `module_alloc`，失败回退 `execmem_alloc(EXECMEM_MODULE_TEXT=1, size)`，
释放时对应回退。若两者都没有，加载会以
`neither module_alloc nor execmem_alloc is available` 明确失败。

> 这张表就是"为什么必须做版本核对"的实例：`module_alloc` 的消失发生在 6.10，
> 而 android16-6.12 / android17-6.18 都在支持矩阵里。编译与 CI 都不会报错，
> 只有真机运行才会暴露。