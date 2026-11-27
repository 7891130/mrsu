package me.weishu.kernelsu.ui.screen.home

import android.content.Intent
import android.widget.Toast
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.R
import me.weishu.kernelsu.magica.MagicaService
import me.weishu.kernelsu.ui.LocalUiMode
import me.weishu.kernelsu.ui.UiMode
import me.weishu.kernelsu.ui.component.dialog.BootloaderWarningDialog
import me.weishu.kernelsu.ui.component.dialog.rememberBootloaderWarningContent
import me.weishu.kernelsu.ui.component.dialog.rememberConfirmDialog
import me.weishu.kernelsu.ui.component.dialog.rememberLoadingDialog
import me.weishu.kernelsu.ui.component.rebootlistpopup.rememberRebootAction
import me.weishu.kernelsu.ui.navigation3.Navigator
import me.weishu.kernelsu.ui.navigation3.Route
import me.weishu.kernelsu.ui.util.lastShownAnnouncementId
import me.weishu.kernelsu.ui.util.markAnnouncementShown
import me.weishu.kernelsu.ui.viewmodel.HomeViewModel
import kotlin.time.Duration.Companion.milliseconds

@Composable
fun HomePager(
    navigator: Navigator,
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    val viewModel = viewModel<HomeViewModel>()
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val uriHandler = LocalUriHandler.current
    val context = LocalContext.current
    val loadingDialog = rememberLoadingDialog()
    val announcementDialog = rememberConfirmDialog(onConfirm = {})
    val announcement = uiState.announcement
    // 新公告首次出现时自动弹一次，同一条公告只提示一次
    LaunchedEffect(announcement?.id) {
        val current = announcement ?: return@LaunchedEffect
        if (!current.isValid || current.id == lastShownAnnouncementId(context)) return@LaunchedEffect
        markAnnouncementShown(current.id, context)
        announcementDialog.showConfirm(
            title = current.displayTitle.ifBlank { "公告" },
            content = current.content.takeIf { it.isNotBlank() },
            markdown = false,
            confirm = "知道了",
        )
    }
    val scope = rememberCoroutineScope()
    val restartAction = rememberRebootAction()
    val latestIsCurrentPage by rememberUpdatedState(isCurrentPage)
    val initialResumeHandled = rememberSaveable { mutableStateOf(false) }

    var hasActivated by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(isCurrentPage) {
        if (isCurrentPage && !hasActivated) {
            hasActivated = true
            viewModel.refresh()
        }
    }

    var showBootloaderWarning by rememberSaveable { mutableStateOf(false) }
    val bootloaderWarning = rememberBootloaderWarningContent(
        message = stringResource(R.string.bootloader_warning_message),
        confirm = stringResource(R.string.bootloader_warning_confirm),
    )
    BootloaderWarningDialog(
        show = showBootloaderWarning,
        content = bootloaderWarning,
        onConfirm = {
            showBootloaderWarning = false
            navigator.push(Route.Install)
        },
        onDismiss = { showBootloaderWarning = false },
    )

    LifecycleResumeEffect(Unit) {
        if (initialResumeHandled.value && latestIsCurrentPage) {
            viewModel.refresh()
        }
        initialResumeHandled.value = true
        onPauseOrDispose { }
    }

    val actions = HomeActions(
        onInstallClick = {
            if (uiState.isLateLoadMode) {
                // 越狱模式跑在已锁 BL 的设备上，进安装页之前先确认一次；
                // 真正的刷写确认在安装页里「直接安装」时再来一次。
                showBootloaderWarning = true
            } else {
                navigator.push(Route.Install)
            }
        },
        onOpenUrl = uriHandler::openUri,
        onJailbreakClick = {
            loadingDialog.showLoading()
            context.startService(Intent(context, MagicaService::class.java))
            // Manager will be force-stopped and restarted by late-load on success.
            // If that doesn't happen within timeout, jailbreak likely failed.
            scope.launch(Dispatchers.IO) {
                delay(30_000.milliseconds)
                withContext(Dispatchers.Main) {
                    loadingDialog.hide()
                    Toast.makeText(context, R.string.jailbreak_timeout, Toast.LENGTH_LONG).show()
                }
            }
        },
        onRestartClick = { restartAction("") },
    )

    when (LocalUiMode.current) {
        UiMode.Miuix -> HomePagerMiuix(
            state = uiState,
            actions = actions,
            bottomInnerPadding = bottomInnerPadding,
        )

        UiMode.Material -> HomePagerMaterial(
            state = uiState,
            actions = actions,
            bottomInnerPadding = bottomInnerPadding,
        )
    }
}
