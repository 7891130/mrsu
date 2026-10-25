package me.weishu.kernelsu.ui.screen.home

import androidx.compose.runtime.Immutable
import me.weishu.kernelsu.KernelVersion
import me.weishu.kernelsu.ui.util.module.LatestVersionInfo

@Immutable
data class HomeUiState(
    val kernelVersion: KernelVersion,
    val ksuVersion: Int?,
    val managerUAPIVersion: Int,
    val kernelUAPIVersion: Int?,
    val lkmMode: Boolean?,
    val isLkmBundled: Boolean,
    val isManager: Boolean,
    val isManagerPrBuild: Boolean,
    val isKernelPrBuild: Boolean,
    val requiresNewKernel: Boolean,
    val requiresNewManager: Boolean,
    val isRootAvailable: Boolean,
    val isSafeMode: Boolean,
    val isLateLoadMode: Boolean,
    val checkUpdateEnabled: Boolean,
    val latestVersionInfo: LatestVersionInfo,
    val currentManagerVersionCode: Long,
    val systemInfo: SystemInfo,
) {
    val isSELinuxPermissive: Boolean
        get() = systemInfo.selinuxStatus == "Permissive"

    val showGkiWarning: Boolean
        get() = ksuVersion != null && lkmMode == false

    // MRSU: 关闭「内嵌 LKM 有可用更新」提示。
    // 上游判据 ksuVersion == currentManagerVersionCode 依赖「管理器版本号 == 内核 LKM 版本号」这一前提，
    // 定制版管理器走 33xxx/35xxx 发布号、内核 LKM 走上游 30000+git 编号，两套体系永不相等，
    // 结果是即使用定制 ko 直装成功（内核已绑 com.mingri.ru、root 工作正常）该提示也会长期常亮，属于误报。
    // 定制版没有「按版本号判断 LKM 是否更新」的真实语义（内嵌 ko 与已刷内核同源），
    // 真需要重刷内核的情形由 requiresNewKernel 对应的 require_kernel_version 卡片承载，
    // 安装页入口本身也始终可通过点击状态卡进入（onInstallClick），因此这里恒为 false。
    // 若后续引入内核侧独立的 LKM 版本标识，可在此处接入真实比较。
    val showLkmUpdate: Boolean
        get() = false

    // Jailbreak mode runs on locked bootloaders, so flashing a boot image would brick the device.
    val canInstallKernelUpdate: Boolean
        get() = lkmMode == true && !isLateLoadMode

    val showCustomLkmBadge: Boolean
        get() = lkmMode == true && !isLkmBundled

    val showRootWarning: Boolean
        get() = ksuVersion != null && !isRootAvailable

    val showManagerPrBuildWarning: Boolean
        get() = isManager && isManagerPrBuild

    val showKernelPrBuildWarning: Boolean
        get() = isManager && !isManagerPrBuild && isKernelPrBuild

    val hasUpdate: Boolean
        get() = latestVersionInfo.versionCode > currentManagerVersionCode
}

@Immutable
data class HomeActions(
    val onInstallClick: () -> Unit,
    val onOpenUrl: (String) -> Unit,
    val onJailbreakClick: () -> Unit = {},
)
