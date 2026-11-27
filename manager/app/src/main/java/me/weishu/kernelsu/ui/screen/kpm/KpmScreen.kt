package me.weishu.kernelsu.ui.screen.kpm

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
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
import me.weishu.kernelsu.ui.util.insmodReport
import me.weishu.kernelsu.ui.util.kpmCtl0Report
import me.weishu.kernelsu.ui.util.kpmDoctorReport
import me.weishu.kernelsu.ui.util.kpmInfoReport
import me.weishu.kernelsu.ui.util.kpmInstallReport
import me.weishu.kernelsu.ui.util.kpmInsmodRuntimeReport
import me.weishu.kernelsu.ui.util.kpmInstalled
import me.weishu.kernelsu.ui.util.kpmList
import me.weishu.kernelsu.ui.util.kpmSetEnabledReport
import me.weishu.kernelsu.ui.util.kpmUninstallReport
import me.weishu.kernelsu.ui.util.kpmUnloadReport
import me.weishu.kernelsu.ui.util.rootAvailable
import java.io.File

/** 最低带 `KSU_IOCTL_KPM` 的 UAPI 版本，与内核侧保持一致。 */
private const val KPM_MIN_UAPI_VERSION = 7

/**
 * 安装目录里的一个模块（APatch 的布局：`/data/adb/kpm/<id>/<id>.kpm`，加一个
 * `disable` 标记文件表示"装着但开机不加载"），外加"临时加载、没有装进目录"的情况。
 */
private data class KpmEntry(
    val id: String,
    val name: String,
    val version: String,
    val author: String,
    val description: String,
    val disabled: Boolean,
    val loaded: Boolean,
    val installed: Boolean,
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
            disabled = f[4].trim() == "1",
            loaded = f[5].trim() == "1",
            installed = true,
        )
    }
    .toList()

/** `ksud kpm list` 一行：name、version、args —— 用来补上"临时加载但没安装"的模块。 */
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
                disabled = false,
                loaded = true,
                installed = false,
            )
        }
        .toList()

    return entries + extra
}

/** What to do with a picked file. */
private enum class PickedMode { InstallAndLoad, InstallOnly, InsmodKo }

/** Copy a picked document into the app cache so ksud can open a real path. */
private fun Uri.copyIntoCache(context: Context): String {
    val name = getFileName(context)?.takeIf { it.endsWith(".kpm") || it.endsWith(".ko") }
        ?: "module-${System.currentTimeMillis()}.bin"
    val target = File(context.cacheDir, name)
    context.contentResolver.openInputStream(this)?.use { input ->
        target.outputStream().use { output -> input.copyTo(output) }
    } ?: throw IllegalStateException("cannot open $this")
    return target.absolutePath
}

/**
 * KPM（KernelPatch 模块）主界面。
 *
 * 结构与加载方式对齐 APatch：模块**安装**进 `/data/adb/kpm/<id>/<id>.kpm`，
 * 每行右侧的开关控制"开机是否加载"（`<id>/disable` 标记文件），删除时先卸载再删目录；
 * 临时加载（没走安装目录）的模块也会出现在列表里，标注为"临时加载"。
 *
 * 所有操作都走 root shell 调 `ksud kpm ...`，失败原因原样显示在「最近结果」里。
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

    var standaloneLoaded by remember { mutableStateOf(false) }

    fun refresh() {
        scope.launch {
            withContext(Dispatchers.IO) {
                installedText = runCatching { kpmInstalled() }.getOrDefault("")
                loadedText = runCatching { kpmList() }.getOrDefault("")
                standaloneLoaded = File("/sys/module/kpm").exists()
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

    fun handlePicked(uri: Uri?, mode: PickedMode) {
        if (uri == null) return
        runAction {
            val path = uri.copyIntoCache(context)
            when (mode) {
                PickedMode.InstallAndLoad -> kpmInstallReport(path, loadNow = true)
                PickedMode.InstallOnly -> kpmInstallReport(path, loadNow = false)
                PickedMode.InsmodKo -> insmodReport(path)
            }
        }
    }

    val installLoader = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        handlePicked(uri, PickedMode.InstallAndLoad)
    }
    val installOnlyLoader = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        handlePicked(uri, PickedMode.InstallOnly)
    }
    val koPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        handlePicked(uri, PickedMode.InsmodKo)
    }

    val entries = remember(installedText, loadedText) {
        mergeLoaded(parseInstalled(installedText), loadedText)
    }
    val visible = remember(entries, search) {
        if (search.isBlank()) entries
        else entries.filter {
            it.name.contains(search, true) || it.id.contains(search, true)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = bottomInnerPadding + 16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = stringResource(R.string.kpm_page_title),
                style = MaterialTheme.typography.titleLarge,
            )
            TextButton(onClick = { reloadToken++ }) {
                Text(stringResource(R.string.kpm_refresh))
            }
        }

        // Which directory the list comes from. It doubles as a build marker: an
        // older APK has no such line, so "no change" can be told apart from
        // "old build still installed".
        Text(
            text = stringResource(R.string.kpm_store_hint, "/data/adb/kpm"),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        unavailableReason?.let { reason ->
            SectionCard(title = stringResource(R.string.kpm_unavailable)) {
                Text(text = reason, style = MaterialTheme.typography.bodyMedium)
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(
                modifier = Modifier.weight(1f),
                enabled = enabled,
                onClick = { installLoader.launch(arrayOf("*/*")) },
            ) {
                Text(stringResource(R.string.kpm_import_load))
            }
            OutlinedButton(
                modifier = Modifier.weight(1f),
                enabled = enabled,
                onClick = { installOnlyLoader.launch(arrayOf("*/*")) },
            ) {
                Text(stringResource(R.string.kpm_import_only))
            }
        }

        OutlinedTextField(
            value = search,
            onValueChange = { search = it },
            modifier = Modifier.fillMaxWidth(),
            enabled = entries.size > 3,
            singleLine = true,
            label = { Text(stringResource(R.string.kpm_search)) },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
        )

        if (entries.isEmpty()) {
            Text(
                text = stringResource(R.string.kpm_empty),
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(vertical = 24.dp),
            )
            // An empty list has two very different causes: nothing is installed,
            // or `ksud kpm installed` could not answer at all. Show its words
            // instead of a bare "no modules".
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
                    onToggle = { on ->
                        runAction { kpmSetEnabledReport(entry.id, on) }
                    },
                    onControl = { controlTarget = entry },
                    onInfo = { runAction { kpmInfoReport(entry.name) } },
                    onUnload = { unloadTarget = entry },
                    onRemove = { removeTarget = entry },
                )
            }
        }

        SectionCard(title = stringResource(R.string.kpm_integration)) {
            Text(
                text = if (standaloneLoaded) {
                    stringResource(R.string.kpm_standalone_loaded)
                } else {
                    stringResource(R.string.kpm_standalone_idle)
                },
                style = MaterialTheme.typography.bodyMedium,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(
                    enabled = enabled && !standaloneLoaded,
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
                TextButton(
                    enabled = enabled,
                    onClick = { koPicker.launch(arrayOf("*/*")) },
                ) {
                    Text(stringResource(R.string.kpm_insmod_ko))
                }
            }
        }

        if (resultText.isNotEmpty()) {
            SectionCard(title = stringResource(R.string.kpm_last_result)) {
                Text(text = resultText, style = MaterialTheme.typography.bodySmall)
            }
        }

        Text(
            text = stringResource(R.string.kpm_note),
            style = MaterialTheme.typography.bodySmall,
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

/** 一行模块卡片：名称/状态/版本作者、说明，右侧是"开机加载"开关，底部是三个动作。 */
@Composable
private fun ModuleCard(
    entry: KpmEntry,
    enabled: Boolean,
    onToggle: (Boolean) -> Unit,
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
            Row(
                modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    Text(
                        text = entry.name,
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        text = when {
                            !entry.installed -> stringResource(R.string.kpm_temporary_label)
                            entry.disabled -> stringResource(R.string.kpm_disabled_label)
                            else -> stringResource(R.string.kpm_installed_label)
                        },
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary,
                    )
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
                    Text(
                        text = stringResource(R.string.kpm_loaded_label, if (entry.loaded) {
                            stringResource(R.string.kpm_loaded_yes)
                        } else {
                            stringResource(R.string.kpm_loaded_no)
                        }),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }

                if (entry.installed) {
                    Switch(checked = !entry.disabled, enabled = enabled, onCheckedChange = onToggle)
                }
            }

            if (entry.description.isNotEmpty()) {
                Text(
                    text = entry.description,
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 6.dp),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.outline,
                )
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
                if (entry.installed) {
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

@Composable
private fun SectionCard(title: String, content: @Composable () -> Unit) {
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
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(text = title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}