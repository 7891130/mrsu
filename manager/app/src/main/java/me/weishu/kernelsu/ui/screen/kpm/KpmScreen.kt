package me.weishu.kernelsu.ui.screen.kpm

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.Dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.LocalUiMode
import me.weishu.kernelsu.ui.UiMode
import me.weishu.kernelsu.ui.util.getFileName
import me.weishu.kernelsu.ui.util.kpmCtl0Report
import me.weishu.kernelsu.ui.util.kpmDoctorReport
import me.weishu.kernelsu.ui.util.kpmInfoReport
import me.weishu.kernelsu.ui.util.kpmInstallReport
import me.weishu.kernelsu.ui.util.kpmInsmodRuntimeReport
import me.weishu.kernelsu.ui.util.kpmInstalled
import me.weishu.kernelsu.ui.util.kpmList
import me.weishu.kernelsu.ui.util.kpmLoadReport
import me.weishu.kernelsu.ui.util.kpmUninstallReport
import me.weishu.kernelsu.ui.util.kpmUnloadReport
import me.weishu.kernelsu.ui.util.rootAvailable
import java.io.File

/** 最低带 `KSU_IOCTL_KPM` 的 UAPI 版本，与内核侧保持一致。 */
internal const val KPM_MIN_UAPI_VERSION = 7

/**
 * 列表里的一项。`embedded` = 躺在内核侧目录（`/data/adb/kpm/<id>/`）里，开机由
 * autoload 重新加载；`loaded` = 此刻在内核内存里。
 */
internal data class KpmEntry(
    val id: String,
    val name: String,
    val version: String,
    val author: String,
    val description: String,
    val embedded: Boolean,
    val loaded: Boolean,
)

internal data class KpmUiStateHolder(
    val entries: List<KpmEntry>,
    val search: String,
    val onSearchChange: (String) -> Unit,
    val unavailableReason: String?,
    val resultText: String,
    val installedText: String,
)

internal data class KpmActions(
    val onRefresh: () -> Unit,
    val onAdd: () -> Unit,
    val onControl: (KpmEntry) -> Unit,
    val onInfo: (KpmEntry) -> Unit,
    val onUnload: (KpmEntry) -> Unit,
    val onRemove: (KpmEntry) -> Unit,
    val onLoadRuntime: () -> Unit,
    val onDoctor: () -> Unit,
)

/** `ksud kpm installed` 一行：id、name、version、author、disabled、loaded、description。 */
private fun parseInstalled(text: String): List<KpmEntry> = text.lineSequence()
    .filter { it.isNotBlank() && it.count { c -> c == '\t' } >= 5 }
    .mapNotNull { line ->
        val f = line.split('\t')
        if (f.size < 7) return@mapNotNull null
        KpmEntry(
            id = f[0].trim(),
            name = f[1].trim().ifEmpty { f[0].trim() },
            version = f[2].trim(),
            author = f[3].trim(),
            description = f[6].trim(),
            embedded = true,
            loaded = f[5].trim() == "1",
        )
    }
    .toList()

/** `ksud kpm list`：补上"加载了但没嵌入"的那些。 */
private fun mergeLoaded(entries: List<KpmEntry>, loadedText: String): List<KpmEntry> {
    val extra = loadedText.lineSequence()
        .filter { it.isNotBlank() }
        .map { it.split('\t') }
        .mapNotNull { f ->
            val name = f.getOrNull(0)?.trim().orEmpty()
            if (name.isEmpty() || entries.any { it.name == name }) return@mapNotNull null
            KpmEntry(
                id = name,
                name = name,
                version = f.getOrNull(1)?.trim().orEmpty(),
                author = "",
                description = f.getOrNull(2)?.trim().orEmpty(),
                embedded = false,
                loaded = true,
            )
        }
        .toList()

    return entries + extra
}

/** Copy a picked document into the app cache so ksud can open a real path. */
private fun Uri.copyIntoCache(context: Context): String {
    val name = getFileName(context)?.takeIf { it.endsWith(".kpm") }
        ?: "module-${System.currentTimeMillis()}.kpm"
    val target = File(context.cacheDir, name)
    context.contentResolver.openInputStream(this)?.use { input ->
        target.outputStream().use { output -> input.copyTo(output) }
    } ?: throw IllegalStateException("cannot open $this")
    return target.absolutePath
}

/**
 * KPM 主界面。和模块页同一套骨架：同样的顶栏、同样的卡片列表、右下角一个「+」。
 *
 * 右下角的加号选好文件之后才问加载方式，只有两项：
 *   - 加载：只进当前内核内存，重启失效（`ksud kpm load`）；
 *   - 嵌入：装进 `/data/adb/kpm/<id>/` 并立即加载，重启后内核自己再加载（`ksud kpm install`）。
 */
@Composable
fun KpmPager(
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var installedText by remember { mutableStateOf("") }
    var loadedText by remember { mutableStateOf("") }
    var resultText by remember { mutableStateOf("") }
    var reloadToken by remember { mutableIntStateOf(0) }
    var search by remember { mutableStateOf("") }
    var pendingPath by remember { mutableStateOf<String?>(null) }
    var controlTarget by remember { mutableStateOf<KpmEntry?>(null) }
    var removeTarget by remember { mutableStateOf<KpmEntry?>(null) }
    var unloadTarget by remember { mutableStateOf<KpmEntry?>(null) }

    val rootAvailable by produceState(initialValue = false) { value = rootAvailable() }
    val kernelUapi = Natives.kernelUAPIVersion
    val unavailableReason = when {
        !Natives.isManager -> context.getString(R.string.kpm_unavailable_not_manager)
        kernelUapi < KPM_MIN_UAPI_VERSION ->
            context.getString(R.string.kpm_unavailable_kernel, kernelUapi, KPM_MIN_UAPI_VERSION)
        !rootAvailable -> context.getString(R.string.kpm_unavailable_root)
        else -> null
    }

    fun refresh() {
        scope.launch {
            withContext(Dispatchers.IO) {
                installedText = runCatching { kpmInstalled() }.getOrDefault("")
                loadedText = runCatching { kpmList() }.getOrDefault("")
            }
        }
    }

    LaunchedEffect(reloadToken, isCurrentPage) {
        if (isCurrentPage) refresh()
    }

    fun runAction(block: () -> String) {
        scope.launch {
            resultText = withContext(Dispatchers.IO) {
                runCatching(block).getOrElse { it.message ?: it.toString() }
            }
            reloadToken++
        }
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            pendingPath = runCatching { uri.copyIntoCache(context) }.getOrNull()
        }
    }

    val entries = remember(installedText, loadedText) {
        mergeLoaded(parseInstalled(installedText), loadedText)
    }

    val uiState = KpmUiStateHolder(
        entries = entries,
        search = search,
        onSearchChange = { search = it },
        unavailableReason = unavailableReason,
        resultText = resultText,
        installedText = installedText,
    )
    val actions = KpmActions(
        onRefresh = { reloadToken++ },
        onAdd = { picker.launch(arrayOf("*/*")) },
        onControl = { controlTarget = it },
        onInfo = { runAction { kpmInfoReport(it.name) } },
        onUnload = { unloadTarget = it },
        onRemove = { removeTarget = it },
        onLoadRuntime = { runAction { kpmInsmodRuntimeReport() } },
        onDoctor = { runAction { kpmDoctorReport() } },
    )

    when (LocalUiMode.current) {
        UiMode.Miuix -> KpmPagerMiuix(uiState, actions, bottomInnerPadding)
        UiMode.Material -> KpmPagerMaterial(uiState, actions, bottomInnerPadding)
    }

    pendingPath?.let { path ->
        LoadMethodDialog(
            fileName = File(path).name,
            enabled = unavailableReason == null,
            onDismiss = { pendingPath = null },
            onLoad = {
                pendingPath = null
                runAction { kpmLoadReport(path) }
            },
            onEmbed = {
                pendingPath = null
                runAction { kpmInstallReport(path, loadNow = true) }
            },
        )
    }

    controlTarget?.let { entry ->
        ControlDialog(
            entry = entry,
            enabled = unavailableReason == null,
            onDismiss = { controlTarget = null },
            onRun = { args ->
                controlTarget = null
                runAction { kpmCtl0Report(entry.name, args) }
            },
        )
    }

    unloadTarget?.let { entry ->
        ConfirmDialog(
            title = context.getString(R.string.kpm_unload),
            message = context.getString(R.string.kpm_unload_message, entry.name),
            confirm = context.getString(R.string.kpm_unload),
            onDismiss = { unloadTarget = null },
            onConfirm = {
                unloadTarget = null
                runAction { kpmUnloadReport(entry.name) }
            },
        )
    }

    removeTarget?.let { entry ->
        ConfirmDialog(
            title = context.getString(R.string.kpm_remove),
            message = context.getString(R.string.kpm_remove_confirm, entry.name, entry.id),
            confirm = context.getString(R.string.kpm_remove),
            onDismiss = { removeTarget = null },
            onConfirm = {
                removeTarget = null
                runAction { kpmUninstallReport(entry.id) }
            },
        )
    }
}