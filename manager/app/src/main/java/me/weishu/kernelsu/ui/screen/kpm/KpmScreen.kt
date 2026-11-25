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
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.util.getFileName
import me.weishu.kernelsu.ui.util.insmodReport
import me.weishu.kernelsu.ui.util.kpmList
import me.weishu.kernelsu.ui.util.kpmLoadReport
import me.weishu.kernelsu.ui.util.kpmStageReport
import me.weishu.kernelsu.ui.util.kpmStaged
import me.weishu.kernelsu.ui.util.kpmUnstage
import java.io.File

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
 * KernelPatch module (.kpm) overview.
 *
 * Everything goes through `ksud kpm ...` in a root shell, the same channel the
 * module list uses. The runtime reports failures (unknown symbol, bad image,
 * unsupported relocation) on that channel, so the text is shown as-is.
 */
@Composable
fun KpmScreen() {
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var loadedText by remember { mutableStateOf("") }
    var stagedText by remember { mutableStateOf("") }
    var resultText by remember { mutableStateOf("") }
    var reloadToken by remember { mutableIntStateOf(0) }

    fun refresh() {
        scope.launch {
            withContext(Dispatchers.IO) {
                loadedText = runCatching { kpmList() }.getOrDefault("")
                stagedText = runCatching { kpmStaged() }.getOrDefault("")
            }
        }
    }

    LaunchedEffect(reloadToken) { refresh() }

    fun handlePicked(uri: Uri?, mode: PickedMode) {
        if (uri == null) return
        scope.launch {
            val report = withContext(Dispatchers.IO) {
                runCatching {
                    val path = uri.copyIntoCache(context)
                    when (mode) {
                        PickedMode.LoadKpm -> kpmLoadReport(path)
                        PickedMode.StageKpm -> kpmStageReport(path)
                        PickedMode.InsmodKo -> insmodReport(path)
                    }
                }.getOrElse { it.message ?: it.toString() }
            }
            resultText = report
            reloadToken++
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

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = stringResource(R.string.kpm_title),
                style = MaterialTheme.typography.titleLarge,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                TextButton(onClick = { reloadToken++ }) {
                    Text(stringResource(R.string.kpm_refresh))
                }
                TextButton(onClick = dropUnlessResumed { navigator.pop() }) {
                    Text(stringResource(R.string.close))
                }
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(
                modifier = Modifier.weight(1f),
                onClick = { loadPicker.launch(arrayOf("*/*")) },
            ) {
                Text(stringResource(R.string.kpm_load_now))
            }
            OutlinedButton(
                modifier = Modifier.weight(1f),
                onClick = { stagePicker.launch(arrayOf("*/*")) },
            ) {
                Text(stringResource(R.string.kpm_stage_for_boot))
            }
        }

        OutlinedButton(
            modifier = Modifier.fillMaxWidth(),
            onClick = { koPicker.launch(arrayOf("*/*")) },
        ) {
            Text(stringResource(R.string.kpm_insmod_ko))
        }

        if (resultText.isNotEmpty()) {
            SectionCard(title = stringResource(R.string.kpm_last_result)) {
                Text(text = resultText, style = MaterialTheme.typography.bodySmall)
            }
        }

        SectionCard(title = stringResource(R.string.kpm_loaded)) {
            val text = loadedText.trim()
            if (text.isEmpty()) {
                Text(
                    text = stringResource(R.string.kpm_empty),
                    style = MaterialTheme.typography.bodyMedium,
                )
            } else {
                text.lineSequence().filter { it.isNotBlank() }.forEach { line ->
                    val parts = line.split('\t')
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = parts.firstOrNull().orEmpty(),
                            style = MaterialTheme.typography.bodyLarge,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f, fill = false),
                        )
                        if (parts.size > 1) {
                            Text(text = parts[1], style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }

        SectionCard(title = stringResource(R.string.kpm_staged)) {
            val text = stagedText.trim()
            if (text.isEmpty()) {
                Text(
                    text = stringResource(R.string.kpm_staged_empty),
                    style = MaterialTheme.typography.bodyMedium,
                )
            } else {
                text.lineSequence().filter { it.isNotBlank() }.forEach { line ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = line,
                            style = MaterialTheme.typography.bodyLarge,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f, fill = false),
                        )
                        TextButton(
                            onClick = {
                                scope.launch {
                                    withContext(Dispatchers.IO) { kpmUnstage(line) }
                                    reloadToken++
                                }
                            },
                        ) {
                            Text(stringResource(R.string.kpm_unstage))
                        }
                    }
                }
            }
        }

        Text(
            text = stringResource(R.string.kpm_note),
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

@Composable
private fun SectionCard(title: String, content: @Composable () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
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
