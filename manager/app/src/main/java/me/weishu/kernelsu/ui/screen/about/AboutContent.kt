package me.weishu.kernelsu.ui.screen.about

/**
 * 关于页展示的应用介绍与使用教程文案。
 * 品牌：MRSU（ks@明小日），基于 KernelSU 定制。
 */
internal data class AboutEntry(
    val title: String,
    val desc: String,
)

internal const val ABOUT_INTRO_TITLE = "应用介绍"
internal const val ABOUT_TUTORIAL_TITLE = "使用教程"

internal const val ABOUT_INTRO_SUMMARY =
    "MRSU 是基于 KernelSU 定制的内核级 Root 管理器，包名 com.mingri.ru，署名 ks@明小日，" +
        "仅在本机自用环境发布，与官方 KernelSU 管理器互不冲突。"

internal val ABOUT_INTRO: List<AboutEntry> = listOf(
    AboutEntry(
        "内核态 Root",
        "由定制内核与 ksuinit 在开机阶段接管，Root 权限归属于本应用，按 UID 精确授权，不依赖用户态守护进程。",
    ),
    AboutEntry(
        "超级用户",
        "应用请求 Root 时弹窗确认，可随时在「超级用户」页开关单个应用的权限，支持按包名查看授权记录。",
    ),
    AboutEntry(
        "模块系统",
        "沿用 KernelSU 模块规范，支持 zip 安装、模块开关与更新，模块在重启后生效。",
    ),
    AboutEntry(
        "刷入方式",
        "支持直接安装（无镜像时由 ksud 生成 boot 镜像）与本地镜像刷入；写入动作交由 ksud 完成，管理器本身不改写分区。",
    ),
    AboutEntry(
        "界面定制",
        "内置默认壁纸、悬浮底栏与液态玻璃效果，可切换深浅色、Monet 取色、导航栏角标与页面切换手势。",
    ),
)

internal val ABOUT_TUTORIAL: List<AboutEntry> = listOf(
    AboutEntry(
        "1. 确认内核侧已生效",
        "设备需刷入与本机 KMI 匹配的 MRSU 内核。打开应用首页，若状态为「已安装」表示 ksuinit 已接管；" +
            "显示「未安装」说明内核侧未生效，请检查内核版本与 KMI 是否一致。",
    ),
    AboutEntry(
        "2. 安装与升级管理器",
        "安装本 APK，同签名可直接覆盖升级，原有授权与设置会保留。若需要重置为默认外观，清除应用数据即可。",
    ),
    AboutEntry(
        "3. 授予 Root 权限",
        "进入「超级用户」页，应用首次请求时确认授权；也可在该页手动开关任意应用的权限，关闭后立即失效。",
    ),
    AboutEntry(
        "4. 安装模块",
        "在「模块」页选择模块 zip 安装，安装完成后重启生效；出现异常时可在同一页面禁用或卸载模块。",
    ),
    AboutEntry(
        "5. 更换或刷入内核",
        "优先使用「直接安装」由 ksud 生成镜像后刷入；也可在本地选择 boot 镜像。请勿用第三方工具手动改写分区，" +
            "写错活动槽会导致无法开机。",
    ),
    AboutEntry(
        "6. 个性化界面",
        "设置 → 主题设置，可切换颜色模式与 Monet 取色，开关悬浮底栏、液态玻璃、导航栏角标及返回手势。",
    ),
    AboutEntry(
        "7. 出问题怎么恢复",
        "无法开机时先用 fastboot 刷回原厂 boot 镜像恢复系统；需要排查时在 设置 → 发送日志 导出日志。",
    ),
)
