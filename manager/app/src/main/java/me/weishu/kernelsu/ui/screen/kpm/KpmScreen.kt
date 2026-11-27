package me.weishu.kernelsu.ui.screen.kpm

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
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
import me.weishu.kernelsu.ui.util.kpmInsmodRuntimeReport
import me.weishu.kernelsu.ui.util.kpmList
import me.weishu.kernelsu.ui.util.kpmLoadReport
import me.weishu.kernelsu.ui.util.kpmStageReport
import me.weishu.kernelsu.ui.util.kpmStaged
import me.weishu.kernelsu.ui.util.kpmUnloadReport
import me.weishu.kernelsu.ui.util.kpmUnstageReport
import me.weishu.kernelsu.ui.util.rootAvailable
import java.io.File

/** 最低带 `KSU_IOCTL_KPM` 的 UAPI 版本，与内核侧保持一致。 */
private const val KPM_MIN_UAPI_VERSION = 7

/** 一个已加载模块，来自 `ksud kpm list` 的一行。 */
private data class LoadedModule(val name: String, val version: String, val args: String)

private fun parseLoaded(text: String): List<LoadedModule> = text.lineSequence()
    .filter { it.isNotBlank() }
    .map { line ->
        val parts = line.split('\t')
        LoadedModule(
            name = parts.getOrNull(0).orEmpty(),
            version = parts.getOrNull(1).orEmpty(),
            args = parts.getOrNull(2).orEmpty(),
        )
    }
    .toList()

/** What to do with a picked file. */
private enum class PickedMode { LoadKpm, StageKpm, InsmodKo }

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
 * KPM（KernelPatch 模块）主界面，位于底部导航。
 *
 * 结构对齐同类管理器：顶部「刷新」，下面「导入 / 开机加载」，已加载模块逐项列出
 * 名称、版本、作者/参数，并提供「控制 / 详情 / 卸载」；再下面是开机清单和
 * 「独立运行时」卡片（kpm.ko 的加载与诊断）。
 *
 * 所有操作都通过 root shell 调 `ksud kpm ...`，和模块列表用的是同一条通道；
 * 失败原因（未知符号、镜像不合法、重定位不支持）原样显示，因为这类信息最有用。
 */
@Composable
fun KpmPager(
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var loadedText by remember { mutableStateOf("") }
    var stagedText by remember { mutableStateOf("") }
    var resultText by remember { mutableStateOf("") }
    var reloadToken by remember { mutableIntStateOf(0) }
    var controlTarget by remember { mutableStateOf<LoadedModule?>(null) }
    var unloadTarget by remember { mutableStateOf<LoadedModule?>(null) }

    val rootAvailable by produceState(initialValue = false) { value = rootAvailable() }
    val kernelUapi = Natives.kernelUAPIVersion
    val unavailableReason = when {
        !Natives.isManager -> stringResource(R.string.kpm_unavailable_not_manager)
        kernelUapi < KPM_MIN_UAPI_VERSION ->
            stringResource(R.string.kpm_unavailable_kernel, kernelUapi, KPM_MIN_UAPI_VERSION)
        !rootAvailable -> stringResource(R.string.kpm_unavailable_root)
        else -> null
    }

    // kpm.ko 是独立内核模块，加载后 /sys/module/kpm 存在；用它来显示运行时的形态。
    var standaloneLoaded by remember { mutableStateOf(false) }

    fun refresh() {
        scope.launch {
            withContext(Dispatchers.IO) {
                loadedText = runCatching { kpmList() }.getOrDefault("")
                stagedText = runCatching { kpmStaged() }.getOrDefault("")
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
                PickedMode.LoadKpm -> kpmLoadReport(path)
                PickedMode.StageKpm -> kpmStageReport(path)
                PickedMode.InsmodKo -> insmodReport(path)
            }
        }
    }

    val loadPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        handlePicked(uri, PickedMode.LoadKpm)
    }
    val stagePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        handlePicked(uri, PickedMode.StageKpm)
    }
    val koPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        handlePicked(uri, PickedMode.InsmodKo)
    }

    val modules = remember(loadedText) { parseLoaded(loadedText) }
    val staged = remember(stagedText) {
        stagedText.lineSequence().filter { it.isNotBlank() }.toList()
    }
    val enabled = unavailableReason == null

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
                onClick = { loadPicker.launch(arrayOf("*/*")) },
            ) {
                Text(stringResource(R.string.kpm_import))
            }
            OutlinedButton(
                modifier = Modifier.weight(1f),
                enabled = enabled,
                onClick = { stagePicker.launch(arrayOf("*/*")) },
            ) {
                Text(stringResource(R.string.kpm_stage_for_boot))
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

        SectionCard(title = stringResource(R.string.kpm_loaded)) {
            if (modules.isEmpty()) {
                Text(
                    text = stringResource(R.string.kpm_empty),
                    style = MaterialTheme.typography.bodyMedium,
                )
            } else {
                modules.forEachIndexed { index, module ->
                    if (index > 0) HorizontalDivider()
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(2.dp),
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                text = module.name,
                                style = MaterialTheme.typography.bodyLarge,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f, fill = false),
                            )
                            if (module.version.isNotEmpty()) {
                                Text(
                                    text = module.version,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                        }
                        if (module.args.isNotEmpty()) {
                            Text(
                                text = stringResource(R.string.kpm_parameters, module.args),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            TextButton(
                                enabled = enabled,
                                onClick = {
                                    controlTarget = module
                                },
                            ) {
                                Text(stringResource(R.string.kpm_control))
                            }
                            TextButton(
                                enabled = enabled,
                                onClick = { runAction { kpmInfoReport(module.name) } },
                            ) {
                                Text(stringResource(R.string.kpm_info))
                            }
                            TextButton(
                                enabled = enabled,
                                onClick = { unloadTarget = module },
                            ) {
                                Text(stringResource(R.string.kpm_unload))
                            }
                        }
                    }
                }
            }
        }

        SectionCard(title = stringResource(R.string.kpm_staged)) {
            if (staged.isEmpty()) {
                Text(
                    text = stringResource(R.string.kpm_staged_empty),
                    style = MaterialTheme.typography.bodyMedium,
                )
            } else {
                staged.forEach { name ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = name,
                            style = MaterialTheme.typography.bodyLarge,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f, fill = false),
                        )
                        TextButton(
                            enabled = enabled,
                            onClick = { runAction { kpmUnstageReport(name) } },
                        ) {
                            Text(stringResource(R.string.kpm_remove))
                        }
                    }
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

    controlTarget?.let { module ->
        ControlDialog(
            module = module,
            enabled = enabled,
            onDismiss = { controlTarget = null },
            onRun = { args ->
                controlTarget = null
                runAction { kpmCtl0Report(module.name, args) }
            },
        )
    }

    unloadTarget?.let { module ->
        AlertDialog(
            onDismissRequest = { unloadTarget = null },
            title = { Text(stringResource(R.string.kpm_unload)) },
            text = { Text(stringResource(R.string.kpm_unload_message, module.name)) },
            confirmButton = {
                TextButton(onClick = {
                    unloadTarget = null
                    runAction { kpmUnloadReport(module.name) }
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
}

/** 「控制」弹窗：填参数后跑一次模块的 ctl0。 */
@Composable
private fun ControlDialog(
    module: LoadedModule,
    enabled: Boolean,
    onDismiss: () -> Unit,
    onRun: (String) -> Unit,
) {
    var args by remember(module.name) { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.kpm_control_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(module.name, style = MaterialTheme.typography.bodyMedium)
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
