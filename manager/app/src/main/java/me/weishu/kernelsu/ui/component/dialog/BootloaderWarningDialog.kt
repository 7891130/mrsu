package me.weishu.kernelsu.ui.component.dialog

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.GppBad
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.LocalUiMode
import me.weishu.kernelsu.ui.UiMode
import me.weishu.kernelsu.ui.component.material.ExpressiveDialog
import top.yukonga.miuix.kmp.basic.ButtonDefaults as MiuixButtonDefaults
import top.yukonga.miuix.kmp.basic.Icon as MiuixIcon
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.basic.TextButton as MiuixTextButton
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.window.WindowDialog

/**
 * 越狱模式（late-load）下的「BL 锁」前置确认弹窗。
 *
 * 越狱模式跑在已锁定 Bootloader 的设备上，而刷写 boot 分区一旦失败就是变砖，
 * 所以在进入安装页、以及真正开始刷写之前各拦一次。两处用同一套视觉，
 * 用户看到的是同一个警告，而不是两个风格不同的对话框。
 */
@Immutable
data class BootloaderWarningContent(
    val title: String,
    val message: String,
    val highlights: List<String>,
    val confirm: String,
    val dismiss: String,
)

@Composable
fun rememberBootloaderWarningContent(
    message: String,
    confirm: String,
): BootloaderWarningContent {
    val title = stringResource(R.string.bootloader_warning_title)
    val avbPoint = stringResource(R.string.bootloader_warning_point_avb)
    val dataPoint = stringResource(R.string.bootloader_warning_point_data)
    val dismiss = stringResource(R.string.bootloader_warning_dismiss)
    return remember(title, message, confirm, avbPoint, dataPoint, dismiss) {
        BootloaderWarningContent(
            title = title,
            message = message,
            highlights = listOf(avbPoint, dataPoint),
            confirm = confirm,
            dismiss = dismiss,
        )
    }
}

@Composable
fun BootloaderWarningDialog(
    show: Boolean,
    content: BootloaderWarningContent,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    when (LocalUiMode.current) {
        UiMode.Material -> if (show) BootloaderWarningDialogMaterial(content, onConfirm, onDismiss)
        UiMode.Miuix -> BootloaderWarningDialogMiuix(show, content, onConfirm, onDismiss)
    }
}

@Composable
private fun BootloaderWarningDialogMaterial(
    content: BootloaderWarningContent,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    ExpressiveDialog(
        onDismissRequest = onDismiss,
        icon = {
            WarningBadge(background = MaterialTheme.colorScheme.errorContainer) { modifier ->
                Icon(
                    imageVector = Icons.Rounded.GppBad,
                    contentDescription = null,
                    modifier = modifier,
                    tint = MaterialTheme.colorScheme.onErrorContainer,
                )
            }
        },
        title = {
            Text(
                text = content.title,
                fontWeight = FontWeight.SemiBold,
            )
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    text = content.message,
                    style = MaterialTheme.typography.bodyMedium,
                )
                content.highlights.forEach { line ->
                    Row(modifier = Modifier.fillMaxWidth()) {
                        Text(
                            text = "\u2022",
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.error,
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = line,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        },
        confirmButton = {
            Button(
                onClick = onConfirm,
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.error,
                    contentColor = MaterialTheme.colorScheme.onError,
                ),
            ) {
                Text(text = content.confirm, fontWeight = FontWeight.Medium)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(text = content.dismiss)
            }
        },
    )
}

@Composable
private fun BootloaderWarningDialogMiuix(
    show: Boolean,
    content: BootloaderWarningContent,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    WindowDialog(
        show = show,
        modifier = Modifier.windowInsetsPadding(WindowInsets.systemBars.only(WindowInsetsSides.Top)),
        title = content.title,
        onDismissRequest = onDismiss,
        content = {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    WarningBadge(background = MiuixTheme.colorScheme.primaryContainer) { modifier ->
                        MiuixIcon(
                            imageVector = Icons.Rounded.GppBad,
                            contentDescription = null,
                            modifier = modifier,
                            tint = MiuixTheme.colorScheme.onPrimaryContainer,
                        )
                    }
                    MiuixText(
                        text = content.message,
                        modifier = Modifier.weight(1f),
                        color = MiuixTheme.colorScheme.onSurface,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Medium,
                    )
                }
                content.highlights.forEach { line ->
                    MiuixText(
                        text = "\u2022 $line",
                        modifier = Modifier.padding(start = 2.dp),
                        color = MiuixTheme.colorScheme.onSurfaceContainer,
                        fontSize = 13.sp,
                    )
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    MiuixTextButton(
                        text = content.dismiss,
                        onClick = onDismiss,
                        modifier = Modifier.weight(1f),
                    )
                    MiuixTextButton(
                        text = content.confirm,
                        onClick = onConfirm,
                        modifier = Modifier.weight(1f),
                        colors = MiuixButtonDefaults.textButtonColorsPrimary(),
                    )
                }
            }
        },
    )
}

/** 圆形图标底座：两种 UI 模式共用同一套尺寸，配色由调用方从各自主题取。 */
@Composable
private fun WarningBadge(
    background: Color,
    icon: @Composable (Modifier) -> Unit,
) {
    Box(
        modifier = Modifier
            .size(52.dp)
            .background(background, CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        icon(Modifier.size(28.dp))
    }
}
