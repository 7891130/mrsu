package me.weishu.kernelsu.ui.screen.install

import android.app.Activity
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.dropUnlessResumed
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.R
import me.weishu.kernelsu.getKernelVersion
import me.weishu.kernelsu.ui.LocalUiMode
import me.weishu.kernelsu.ui.UiMode
import me.weishu.kernelsu.ui.component.choosekmidialog.ChooseKmiDialog
import me.weishu.kernelsu.ui.component.dialog.BootloaderWarningDialog
import me.weishu.kernelsu.ui.component.dialog.DownloadDialog
import me.weishu.kernelsu.ui.component.dialog.rememberBootloaderWarningContent
import me.weishu.kernelsu.ui.component.dialog.rememberLoadingDialog
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.navigation3.Route
import me.weishu.kernelsu.ui.screen.flash.FlashIt
import me.weishu.kernelsu.ui.util.LkmSelection
import me.weishu.kernelsu.ui.util.getAvailablePartitions
import me.weishu.kernelsu.ui.util.getCurrentKmi
import me.weishu.kernelsu.ui.util.getDefaultPartition
import me.weishu.kernelsu.ui.util.getSlotSuffix
import me.weishu.kernelsu.ui.util.getSupportedKmis
import me.weishu.kernelsu.ui.util.isAbDevice
import me.weishu.kernelsu.ui.util.probeRemoteBootPartitions
import me.weishu.kernelsu.ui.util.rootAvailable
import top.yukonga.miuix.kmp.basic.SnackbarHostState as MiuixSnackbarHostState

@Composable
fun InstallScreen() {
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val snackbarHost = remember { SnackbarHostState() }
    val miuixSnackbarHost = remember { MiuixSnackbarHostState() }
    val uiMode = LocalUiMode.current
    val scope = rememberCoroutineScope()
    val resources = LocalResources.current
    var probeJob by remember { mutableStateOf<Job?>(null) }
    val loadingDialog = rememberLoadingDialog()

    var installMethod by rememberSaveable { mutableStateOf<InstallMethod?>(null) }
    var downloadDialogShown by rememberSaveable { mutableStateOf(false) }
    var remotePartitions by rememberSaveable { mutableStateOf(emptyList<String>()) }
    var remotePartitionSelectionIndex by rememberSaveable { mutableIntStateOf(0) }
    var lkmSelection by rememberSaveable { mutableStateOf<LkmSelection>(LkmSelection.KmiNone) }
    var partitionSelectionIndex by rememberSaveable { mutableIntStateOf(0) }
    var hasCustomSelected by rememberSaveable { mutableStateOf(false) }
    val showChooseKmiDialog = rememberSaveable { mutableStateOf(false) }
    var advancedOptionsShown by rememberSaveable { mutableStateOf(false) }
    var allowShell by rememberSaveable { mutableStateOf(false) }
    var enableAdb by rememberSaveable { mutableStateOf(false) }
    var forceBackup by rememberSaveable { mutableStateOf(false) }
    // 越狱模式下「直接安装」会真的刷写 boot 分区，开始前再确认一次 BL 已解锁
    var showBootloaderWarning by rememberSaveable { mutableStateOf(false) }
    var bootloaderWarningAcknowledged by rememberSaveable { mutableStateOf(false) }

    val currentKmi by produceState(initialValue = "") { value = getCurrentKmi() }
    // null 表示尚未取回；空列表表示当前 ksud 未内置任何 LKM 模块
    val supportedKmis by produceState<List<String>?>(initialValue = null) { value = getSupportedKmis() }
    val partitions by produceState(initialValue = emptyList()) { value = getAvailablePartitions() }
    val defaultPartition by produceState(initialValue = "") { value = getDefaultPartition() }
    val rootAvailable by produceState(initialValue = false) { value = rootAvailable() }
    val isAbDevice by produceState(initialValue = false) { value = isAbDevice() }
    val isGkiDevice by produceState(initialValue = false) { value = getKernelVersion().isGKI() }
    // 越狱模式（late-load）运行在已锁定 BL 上，任何写入分区的操作都有变砖风险
    val jailbreakMode by produceState(initialValue = false) { value = Natives.isLateLoadMode }
    // 内核端与 ksud 的 UAPI 对不上时，ksud 的每个阶段都会以
    // "UAPI version mismatch ... Please update KernelSU!" 失败——直接安装最后跑的正是这些阶段。
    // 越狱模式没法靠刷写换 ko，只能重启后重新越狱，所以这里先把它拦下来，别让它刷到一半再报错。
    val uapiMismatchInJailbreak = jailbreakMode && Natives.kernelUAPIVersion != Natives.managerUAPIVersion
    val uapiMismatchTip = stringResource(
        R.string.install_jailbreak_uapi_mismatch,
        Natives.kernelUAPIVersion,
        Natives.managerUAPIVersion,
    )
    val partitionWriteBlockedTip = stringResource(id = R.string.jailbreak_mode_partition_blocked)

    val selectFileTip = stringResource(id = R.string.select_file_tip, defaultPartition)
    val selectFileTipNoGki = stringResource(id = R.string.select_file_tip_nogki)
    val downloadFileMsg = stringResource(id = R.string.download_dialog_msg)
    val installMethodOptions = remember(
        rootAvailable, isAbDevice, isGkiDevice, selectFileTip, selectFileTipNoGki, downloadFileMsg,
        jailbreakMode, partitionWriteBlockedTip
    ) {
        buildList {
            // 越狱模式下所有写分区的入口依然展示，但 summary 改为禁用说明，由 UI 层置灰
            val blockedSummary = if (jailbreakMode) partitionWriteBlockedTip else null
            add(
                InstallMethod.SelectFile(
                    summary = blockedSummary ?: if (isGkiDevice) selectFileTip else selectFileTipNoGki
                )
            )
            add(InstallMethod.DownloadFile(summary = blockedSummary ?: downloadFileMsg))
            if (rootAvailable && isGkiDevice) {
                add(InstallMethod.DirectInstall)
                if (isAbDevice) add(InstallMethod.DirectInstallToInactiveSlot)
            }
        }
    }

    val isOta = installMethod is InstallMethod.DirectInstallToInactiveSlot
    val slotSuffix by produceState(initialValue = "", isOta) { value = getSlotSuffix(isOta) }
    val defaultIndex = remember(partitions, defaultPartition) {
        partitions.indexOf(defaultPartition).coerceAtLeast(0)
    }

    LaunchedEffect(partitions, defaultIndex, hasCustomSelected) {
        if (partitions.isEmpty()) return@LaunchedEffect
        if (!hasCustomSelected) {
            partitionSelectionIndex = defaultIndex.coerceIn(0, partitions.lastIndex)
        } else if (partitionSelectionIndex > partitions.lastIndex) {
            partitionSelectionIndex = partitions.lastIndex
        }
    }

    val displayPartitions = remember(partitions, defaultPartition) {
        partitions.map { name -> if (defaultPartition == name) "$name (default)" else name }
    }
    val remoteDisplayPartitions = remember(remotePartitions, defaultPartition) {
        remotePartitions.map { name -> if (defaultPartition == name) "$name (default)" else name }
    }

    fun showMessage(message: String) {
        scope.launch {
            if (uiMode == UiMode.Material) {
                snackbarHost.showSnackbar(message)
            } else {
                miuixSnackbarHost.showSnackbar(message)
            }
        }
    }

    val startFlash: (InstallMethod) -> Unit = flash@{ method ->
        val target = when (method) {
            is InstallMethod.DownloadFile -> FlashIt.DownloadBoot(
                url = method.url ?: return@flash,
                partition = method.partition ?: return@flash,
                lkm = lkmSelection,
                allowShell = allowShell,
                enableAdb = enableAdb,
                backup = forceBackup
            )
            else -> FlashIt.FlashBoot(
                boot = if (method is InstallMethod.SelectFile) method.uri else null,
                lkm = lkmSelection,
                ota = method is InstallMethod.DirectInstallToInactiveSlot,
                partition = partitions.getOrNull(partitionSelectionIndex),
                allowShell = allowShell,
                enableAdb = enableAdb,
                backup = method is InstallMethod.SelectFile && forceBackup
            )
        }
        navigator.push(Route.Flash(target))
    }

    val onInstall: () -> Unit = {
        installMethod?.let { method ->
            when {
                // 越狱模式下最后一次兜底拦截：写分区的安装方式（直接安装除外）不放行
                method.blockedInJailbreak(jailbreakMode) -> showMessage(partitionWriteBlockedTip)
                // 内核端 UAPI 没对上：刷了也是白刷，ksud 后续阶段照样报「请更新」
                uapiMismatchInJailbreak -> showMessage(uapiMismatchTip)
                // 直接安装会真的刷写 boot 分区，刷之前再问一次 BL 是否已解锁
                jailbreakMode && method is InstallMethod.DirectInstall && !bootloaderWarningAcknowledged ->
                    showBootloaderWarning = true
                else -> startFlash(method)
            }
        }
    }

    val bootloaderWarningContent = rememberBootloaderWarningContent(
        message = stringResource(R.string.bootloader_warning_direct_install_message),
        confirm = stringResource(R.string.bootloader_warning_confirm_flash),
    )
    BootloaderWarningDialog(
        show = showBootloaderWarning,
        content = bootloaderWarningContent,
        onConfirm = {
            showBootloaderWarning = false
            bootloaderWarningAcknowledged = true
            installMethod?.let { startFlash(it) }
        },
        onDismiss = { showBootloaderWarning = false },
    )

    ChooseKmiDialog(
        show = showChooseKmiDialog.value,
        onDismissRequest = { showChooseKmiDialog.value = false },
        onSelected = { kmi ->
            // kmi 为 null 表示用户选择"不使用 LKM"（内置模式修补）
            if (kmi != null) {
                lkmSelection = LkmSelection.KmiString(kmi)
            } else {
                lkmSelection = LkmSelection.KmiNone
            }
            onInstall()
        }
    )

    DownloadDialog(
        show = downloadDialogShown,
        onConfirm = { url ->
            downloadDialogShown = false
            probeJob?.cancel()
            probeJob = scope.launch {
                try {
                    loadingDialog.showLoading()
                    val result = probeRemoteBootPartitions(url)
                    if (result.partitions.isEmpty()) {
                        showMessage(resources.getString(R.string.download_no_boot_partition))
                    } else {
                        val defaultIdx = result.partitions.indexOf(defaultPartition).coerceAtLeast(0)
                        remotePartitions = result.partitions
                        remotePartitionSelectionIndex = defaultIdx
                        installMethod = InstallMethod.DownloadFile(
                            url = url,
                            partition = result.partitions[defaultIdx],
                            summary = downloadFileMsg,
                        )
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    showMessage(
                        resources.getString(R.string.download_probe_failed, e.message ?: "")
                    )
                } finally {
                    loadingDialog.hide()
                }
            }
        },
        onDismiss = { downloadDialogShown = false }
    )

    val selectLkmLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        if (it.resultCode == Activity.RESULT_OK) {
            it.data?.data?.let { uri ->
                if (isKoFile(context, uri)) {
                    lkmSelection = LkmSelection.LkmUri(uri)
                } else {
                    lkmSelection = LkmSelection.KmiNone
                    showMessage(resources.getString(R.string.install_only_support_ko_file))
                }
            }
        }
    }
    val selectImageLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        if (it.resultCode == Activity.RESULT_OK) {
            it.data?.data?.let { uri ->
                // 越狱模式下不允许通过任何途径进入写分区的安装方式
                if (jailbreakMode) {
                    showMessage(partitionWriteBlockedTip)
                } else {
                    installMethod = InstallMethod.SelectFile(uri, summary = if (isGkiDevice) selectFileTip else selectFileTipNoGki)
                }
            }
        }
    }

    val state = InstallUiState(
        installMethod = installMethod,
        lkmSelection = lkmSelection,
        partitionSelectionIndex = partitionSelectionIndex,
        displayPartitions = displayPartitions,
        remoteDisplayPartitions = remoteDisplayPartitions,
        remotePartitionSelectionIndex = remotePartitionSelectionIndex,
        currentKmi = currentKmi,
        slotSuffix = slotSuffix,
        installMethodOptions = installMethodOptions,
        jailbreakMode = jailbreakMode,
        partitionWriteBlockedTip = partitionWriteBlockedTip,
        canSelectPartition = installMethod is InstallMethod.DirectInstall ||
            installMethod is InstallMethod.DirectInstallToInactiveSlot ||
            installMethod is InstallMethod.DownloadFile,
        advancedOptionsShown = advancedOptionsShown,
        allowShell = allowShell,
        enableAdb = enableAdb,
        forceBackup = forceBackup,
        canForceBackup = installMethod is InstallMethod.SelectFile,
    )
    val actions = InstallScreenActions(
        onBack = dropUnlessResumed { navigator.pop() },
        onSelectMethod = { method ->
            if (method.blockedInJailbreak(jailbreakMode)) {
                showMessage(partitionWriteBlockedTip)
            } else {
                installMethod = method
                // 换一种安装方式就重新确认一次，别让上一次的确认替这次背书
                bootloaderWarningAcknowledged = false
            }
        },
        onDownloadFile = {
            if (jailbreakMode) showMessage(partitionWriteBlockedTip) else downloadDialogShown = true
        },
        onSelectBootImage = {
            if (jailbreakMode) {
                showMessage(partitionWriteBlockedTip)
            } else {
                selectImageLauncher.launch(Intent(Intent.ACTION_GET_CONTENT).apply { type = "application/octet-stream" })
            }
        },
        onUploadLkm = {
            selectLkmLauncher.launch(Intent(Intent.ACTION_GET_CONTENT).apply { type = "application/octet-stream" })
        },
        onClearLkm = { lkmSelection = LkmSelection.KmiNone },
        onSelectPartition = { index ->
            hasCustomSelected = true
            val method = installMethod
            if (method is InstallMethod.DownloadFile) {
                remotePartitionSelectionIndex = index
                installMethod = method.copy(partition = remotePartitions.getOrNull(index))
            } else {
                partitionSelectionIndex = index
            }
        },
        onNext = {
            val isLkmSelected = lkmSelection != LkmSelection.KmiNone
            val isKmiUnknown = currentKmi.isBlank()
            val isKmiUnresolved = when (installMethod) {
                // The download flow extracts the KMI itself; no manual
                // selection needed.
                is InstallMethod.DownloadFile -> false
                // 本地镜像 / 直接安装：未 root 时本地推断出的 KMI 只作为弹窗预选高亮，
                // 一律弹窗让用户确认，避免「猜」出的 KMI 被静默采用。
                else -> isKmiUnknown || !rootAvailable
            }
            val needsKmi = !isLkmSelected && isKmiUnresolved
            when {
                !needsKmi -> onInstall()
                else -> showChooseKmiDialog.value = true
            }
        },
        onAdvancedOptionsClicked = {
            advancedOptionsShown = !advancedOptionsShown
        },
        onSelectAllowShell = {
            allowShell = it
        },
        onSelectEnableAdb = {
            enableAdb = it
        },
        onSelectForceBackup = {
            forceBackup = it
        }
    )

    when (LocalUiMode.current) {
        UiMode.Miuix -> InstallScreenMiuix(state, actions, miuixSnackbarHost)
        UiMode.Material -> InstallScreenMaterial(state, actions, snackbarHost)
    }
}
