package me.weishu.kernelsu.ui.screen.install

import androidx.compose.runtime.Immutable
import me.weishu.kernelsu.ui.util.LkmSelection

@Immutable
internal data class InstallUiState(
    val installMethod: InstallMethod?,
    val lkmSelection: LkmSelection,
    val partitionSelectionIndex: Int,
    val displayPartitions: List<String>,
    val remotePartitionSelectionIndex: Int,
    val remoteDisplayPartitions: List<String>,
    val currentKmi: String,
    val slotSuffix: String,
    val installMethodOptions: List<InstallMethod>,
    // 越狱模式下会写入分区的安装方式全部置灰禁用
    val jailbreakMode: Boolean = false,
    val partitionWriteBlockedTip: String = "",
    val canSelectPartition: Boolean,
    val advancedOptionsShown: Boolean,
    val allowShell: Boolean,
    val enableAdb: Boolean,
    val forceBackup: Boolean,
    val canForceBackup: Boolean,
)

@Immutable
internal data class InstallScreenActions(
    val onBack: () -> Unit,
    val onSelectMethod: (InstallMethod) -> Unit,
    val onDownloadFile: () -> Unit,
    val onSelectBootImage: () -> Unit,
    val onUploadLkm: () -> Unit,
    val onClearLkm: () -> Unit,
    val onSelectPartition: (Int) -> Unit,
    val onNext: () -> Unit,
    val onAdvancedOptionsClicked: () -> Unit,
    val onSelectAllowShell: (Boolean) -> Unit,
    val onSelectEnableAdb: (Boolean) -> Unit,
    val onSelectForceBackup: (Boolean) -> Unit,
)
