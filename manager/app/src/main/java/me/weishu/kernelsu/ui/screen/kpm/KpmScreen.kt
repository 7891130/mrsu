package me.weishu.kernelsu.ui.screen.kpm

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.R
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
private const val KPM_MIN_UAPI_VERSION = 7

/**
 * 列表里的一项。`embedded` 表示它躺在内核侧目录（`/data/adb/kpm/<id>/`）里，
 * 开机由 autoload 重新加载；`loaded` 表示此刻在内核内存里。
 */
private data class KpmEntry(
    val id: String,
    val name: String,
    val version: String,
    val author: String,
    val description: String,
    val embedded: Boolean,
    val loaded: Boolean,
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
 * KPM 主界面：和模块页同一个样子——一张张卡片列出已有的 KPM，右下角一个「+」。
 *
 * 加号选好文件之后才问加载方式，和 FolkPatch 一样只有两种：
 *   - 加载：只放进当前内核内存，重启就没了（`ksud kpm load`）；
 *   - 嵌入：装进内核侧目录 `/data/adb/kpm/<id>/`，重启后由内核自己再加载
 *           （`ksud kpm install`）。没有单独的"开机加载"入口了。
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
        !Natives.isManager -> stringResource(R.string.kpm_unavailable_not_manager)
        kernelUapi < KPM_MIN_UAPI_VERSION ->
            stringResource(R.string.kpm_unavailable_kernel, kernelUapi, KPM_MIN_UAPI_VERSION)
        !rootAvailable -> stringResource(R.string.kpm_unavailable_root)
        else -> null
    }
    val enabled = unavailableReason == null

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
    val visible = remember(entries, search) {
        if (search.isBlank()) entries
        else entries.filter { it.name.contains(search, true) || it.id.contains(search, true) }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = bottomInnerPadding + 88.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column {
                    Text(
                        text = stringResource(R.string.kpm_page_title),
                        style = MaterialTheme.typography.titleLarge,
                    )
                    Text(
                        text = stringResource(R.string.kpm_store_hint, "/data/adb/kpm"),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                TextButton(onClick = { reloadToken++ }) {
                    Text(stringResource(R.string.kpm_refresh))
                }
            }

            unavailableReason?.let { reason ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer,
                    ),
                ) {
                    Text(
                        text = reason,
                        modifier = Modifier.padding(12.dp),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onErrorContainer,
                    )
                }
            }

            if (entries.size > 3) {
                OutlinedTextField(
                    value = search,
                    onValueChange = { search = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    label = { Text(stringResource(R.string.kpm_search)) },
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                )
            }

            if (entries.isEmpty()) {
                Text(
                    text = stringResource(R.string.kpm_empty),
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(vertical = 24.dp),
                )
                // 空列表有两种原因：真没装，或者 ksud 根本答不上来。把原话贴出来。
                if (installedText.isNotBlank() && installedText != resultText) {
                    Text(
                        text = installedText,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            } else {
                visible.forEach { entry ->
                    ModuleCard(
                        entry = entry,
                        enabled = enabled,
                        onControl = { controlTarget = entry },
                        onInfo = { runAction { kpmInfoReport(entry.name) } },
                        onUnload = { unloadTarget = entry },
                        onRemove = { removeTarget = entry },
                    )
                }
            }

            if (resultText.isNotEmpty()) {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.55f),
                    ),
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(12.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp),
                    ) {
                        Text(
                            text = stringResource(R.string.kpm_last_result),
                            style = MaterialTheme.typography.titleSmall,
                        )
                        Text(text = resultText, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(
                    enabled = enabled,
                    onClick = { runAction { kpmInsmodRuntimeReport() } },
                ) {
                    Text(stringResource(R.string.kpm_standalone_load))
                }
                TextButton(
                    enabled = enabled,
                    onClick = { runAction { kpmDoctorReport() } },
                ) {
                    Text(stringResource(R.string.kpm_doctor))
                }
            }
        }

        ExtendedFloatingActionButton(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 16.dp, bottom = bottomInnerPadding + 16.dp),
            onClick = { picker.launch(arrayOf("*/*")) },
            icon = { Icon(Icons.Rounded.Add, contentDescription = null) },
            text = { Text(stringResource(R.string.kpm_add)) },
        )
    }

    pendingPath?.let { path ->
        LoadMethodDialog(
            fileName = File(path).name,
            enabled = enabled,
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
            enabled = enabled,
            onDismiss = { controlTarget = null },
            onRun = { args ->
                controlTarget = null
                runAction { kpmCtl0Report(entry.name, args) }
            },
        )
    }

    unloadTarget?.let { entry ->
        AlertDialog(
            onDismissRequest = { unloadTarget = null },
            title = { Text(stringResource(R.string.kpm_unload)) },
            text = { Text(stringResource(R.string.kpm_unload_message, entry.name)) },
            confirmButton = {
                TextButton(onClick = {
                    unloadTarget = null
                    runAction { kpmUnloadReport(entry.name) }
                }) {
                    Text(stringResource(R.string.kpm_unload))
                }
            },
            dismissButton = {
                TextButton(onClick = { unloadTarget = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            },
        )
    }

    removeTarget?.let { entry ->
        AlertDialog(
            onDismissRequest = { removeTarget = null },
            title = { Text(stringResource(R.string.kpm_remove)) },
            text = { Text(stringResource(R.string.kpm_remove_confirm, entry.name)) },
            confirmButton = {
                TextButton(onClick = {
                    removeTarget = null
                    runAction { kpmUninstallReport(entry.id) }
                }) {
                    Text(stringResource(R.string.kpm_remove))
                }
            },
            dismissButton = {
                TextButton(onClick = { removeTarget = null }) {
                    Text(stringResource(android.R.string.cancel))
                }
            },
        )
    }
}

/** 选好文件之后才问怎么加载，只给「加载」和「嵌入」两项。 */
@Composable
private fun LoadMethodDialog(
    fileName: String,
    enabled: Boolean,
    onDismiss: () -> Unit,
    onLoad: () -> Unit,
    onEmbed: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.kpm_method_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(fileName, style = MaterialTheme.typography.bodyMedium)
                MethodOption(
                    label = stringResource(R.string.kpm_method_load),
                    summary = stringResource(R.string.kpm_method_load_summary),
                    enabled = enabled,
                    onClick = onLoad,
                )
                MethodOption(
                    label = stringResource(R.string.kpm_method_embed),
                    summary = stringResource(R.string.kpm_method_embed_summary),
                    enabled = enabled,
                    onClick = onEmbed,
                )
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(android.R.string.cancel))
            }
        },
    )
}

@Composable
private fun MethodOption(
    label: String,
    summary: String,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    Surface(
        onClick = onClick,
        enabled = enabled,
        shape = MaterialTheme.shapes.medium,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(label, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Medium)
            Text(
                text = summary,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

/** 一行模块卡片，和模块页的观感一致。 */
@Composable
private fun ModuleCard(
    entry: KpmEntry,
    enabled: Boolean,
    onControl: () -> Unit,
    onInfo: () -> Unit,
    onUnload: () -> Unit,
    onRemove: () -> Unit,
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 1.dp,
        shape = MaterialTheme.shapes.large,
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 14.dp),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = entry.name,
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f, fill = false),
                    )
                    Spacer(Modifier.width(8.dp))
                    Text(
                        text = when {
                            !entry.embedded -> stringResource(R.string.kpm_status_loaded_only)
                            entry.loaded -> stringResource(R.string.kpm_status_embedded_loaded)
                            else -> stringResource(R.string.kpm_status_embedded)
                        },
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary,
                    )
                }
                if (entry.version.isNotEmpty() || entry.author.isNotEmpty()) {
                    Text(
                        text = if (entry.author.isNotEmpty()) {
                            stringResource(R.string.kpm_version_author, entry.version, entry.author)
                        } else {
                            entry.version
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                if (entry.description.isNotEmpty()) {
                    Text(
                        text = entry.description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.outline,
                    )
                }
            }

            HorizontalDivider(
                modifier = Modifier.padding(top = 10.dp),
                color = MaterialTheme.colorScheme.surfaceVariant,
            )

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 8.dp, vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Spacer(Modifier.weight(1f))
                TextButton(enabled = enabled, onClick = onControl) {
                    Text(stringResource(R.string.kpm_control))
                }
                TextButton(enabled = enabled, onClick = onInfo) {
                    Text(stringResource(R.string.kpm_info))
                }
                if (entry.loaded) {
                    TextButton(enabled = enabled, onClick = onUnload) {
                        Text(stringResource(R.string.kpm_unload))
                    }
                }
                if (entry.embedded) {
                    TextButton(enabled = enabled, onClick = onRemove) {
                        Text(stringResource(R.string.kpm_remove))
                    }
                }
            }
        }
    }
}

/** 「控制」弹窗：填参数后跑一次模块的 ctl0。 */
@Composable
private fun ControlDialog(
    entry: KpmEntry,
    enabled: Boolean,
    onDismiss: () -> Unit,
    onRun: (String) -> Unit,
) {
    var args by remember(entry.name) { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.kpm_control_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(entry.name, style = MaterialTheme.typography.bodyMedium)
                OutlinedTextField(
                    value = args,
                    onValueChange = { args = it },
                    enabled = enabled,
                    label = { Text(stringResource(R.string.kpm_arguments)) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(enabled = enabled, onClick = { onRun(args.trim()) }) {
                Text(stringResource(R.string.kpm_control_run))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(android.R.string.cancel))
            }
        },
    )
}