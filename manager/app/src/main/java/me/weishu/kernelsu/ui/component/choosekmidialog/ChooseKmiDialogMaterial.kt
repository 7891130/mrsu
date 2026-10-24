package me.weishu.kernelsu.ui.component.choosekmidialog

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.material.SegmentedColumn
import me.weishu.kernelsu.ui.component.material.SegmentedRadioItem
import me.weishu.kernelsu.ui.util.getCurrentKmi
import me.weishu.kernelsu.ui.util.getSupportedKmis

@Composable
fun ChooseKmiDialogMaterial(
    show: Boolean,
    onDismissRequest: () -> Unit,
    onSelected: (String?) -> Unit
) {
    if (!show) return

    val supportedKMIs by produceState(initialValue = emptyList()) {
        value = getSupportedKmis()
    }

    val currentKmi by produceState(initialValue = "") {
        value = getCurrentKmi()
    }

    val selectedKmi = remember(currentKmi) { mutableStateOf(currentKmi) }
    val hasCandidates = supportedKMIs.isNotEmpty()

    AlertDialog(
        onDismissRequest = {
            onDismissRequest()
            selectedKmi.value = currentKmi
        },
        confirmButton = {
            TextButton(
                onClick = {
                    // 无可用 LKM 时以 null 通知调用方走内置模式修补
                    onSelected(if (hasCandidates) selectedKmi.value else null)
                    onDismissRequest()
                },
                enabled = !hasCandidates || supportedKMIs.contains(selectedKmi.value)
            ) {
                Text(
                    stringResource(
                        id = if (hasCandidates) R.string.confirm else R.string.install_builtin_mode
                    )
                )
            }
        },
        dismissButton = {
            TextButton(onClick = {
                onDismissRequest()
                selectedKmi.value = currentKmi
            }) {
                Text(stringResource(id = android.R.string.cancel))
            }
        },
        title = {
            Text(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 16.dp),
                text = stringResource(R.string.select_kmi),
                textAlign = TextAlign.Center
            )
        },
        text = {
            if (hasCandidates) {
                SegmentedColumn(
                    content = supportedKMIs.map { kmi ->
                        {
                            SegmentedRadioItem(
                                title = kmi,
                                summary = if (kmi == currentKmi) stringResource(R.string.current_device_kmi) else null,
                                selected = selectedKmi.value == kmi,
                                onClick = { selectedKmi.value = kmi }
                            )
                        }
                    }
                )
            } else {
                Text(
                    modifier = Modifier.fillMaxWidth(),
                    text = stringResource(R.string.install_no_kmi_available),
                    textAlign = TextAlign.Center
                )
            }
        }
    )
}
