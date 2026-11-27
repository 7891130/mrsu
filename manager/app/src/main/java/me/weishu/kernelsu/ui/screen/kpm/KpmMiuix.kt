package me.weishu.kernelsu.ui.screen.kpm

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import me.weishu.kernelsu.R
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.FloatingActionButton
import top.yukonga.miuix.kmp.basic.HorizontalDivider
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton as MiuixTextButton
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme
import androidx.compose.ui.res.stringResource

/**
 * KPM 页的 Miuix 版。和模块页一样是 `Scaffold` + `TopAppBar` + `Card` 列表，
 * 右下角一个 `FloatingActionButton`，卡片左右 12.dp、底部 12.dp、内边距 16.dp。
 */
@Composable
internal fun KpmPagerMiuix(
    uiState: KpmUiStateHolder,
    actions: KpmActions,
    bottomInnerPadding: Dp,
) {
    val visible = uiState.entries.filter {
        uiState.search.isBlank() ||
            it.name.contains(uiState.search, true) ||
            it.id.contains(uiState.search, true)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.kpm_page_title),
                color = colorScheme.surface,
                actions = {
                    IconButton(onClick = actions.onRefresh) {
                        Icon(
                            imageVector = Icons.Outlined.Refresh,
                            tint = colorScheme.onSurface,
                            contentDescription = stringResource(R.string.kpm_refresh),
                        )
                    }
                },
            )
        },
        floatingActionButton = {
            FloatingActionButton(
                modifier = Modifier.padding(bottom = bottomInnerPadding),
                containerColor = colorScheme.primary,
                onClick = actions.onAdd,
            ) {
                Icon(
                    imageVector = Icons.Filled.Add,
                    tint = colorScheme.onPrimary,
                    contentDescription = stringResource(R.string.kpm_add),
                )
            }
        },
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
            contentPadding = PaddingValues(
                top = 4.dp,
                bottom = bottomInnerPadding + 88.dp,
            ),
        ) {
            item("store") {
                Text(
                    text = stringResource(R.string.kpm_store_hint, "/data/adb/kpm"),
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 6.dp),
                    fontSize = 12.sp,
                    color = colorScheme.onSurfaceContainerVariant,
                )
            }

            uiState.unavailableReason?.let { reason ->
                item("unavailable") {
                    Card(
                        modifier = Modifier
                            .padding(horizontal = 12.dp)
                            .padding(bottom = 12.dp),
                        insideMargin = PaddingValues(16.dp),
                    ) {
                        Text(
                            text = reason,
                            fontSize = 14.sp,
                            color = colorScheme.onSurface,
                        )
                    }
                }
            }

            if (uiState.entries.isEmpty()) {
                item("empty") {
                    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 24.dp)) {
                        Text(
                            text = stringResource(R.string.kpm_empty),
                            fontSize = 15.sp,
                            color = colorScheme.onSurface,
                        )
                        if (uiState.installedText.isNotBlank() && uiState.installedText != uiState.resultText) {
                            Text(
                                text = uiState.installedText,
                                fontSize = 12.sp,
                                color = colorScheme.onSurfaceContainerVariant,
                            )
                        }
                    }
                }
            } else {
                items(visible, key = { it.id }) { entry ->
                    KpmItemMiuix(entry, actions)
                }
            }

            if (uiState.resultText.isNotEmpty()) {
                item("result") {
                    Card(
                        modifier = Modifier
                            .padding(horizontal = 12.dp)
                            .padding(bottom = 12.dp),
                        insideMargin = PaddingValues(16.dp),
                    ) {
                        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            Text(
                                text = stringResource(R.string.kpm_last_result),
                                fontSize = 15.sp,
                                fontWeight = FontWeight.Medium,
                                color = colorScheme.onSurface,
                            )
                            Text(
                                text = uiState.resultText,
                                fontSize = 12.sp,
                                color = colorScheme.onSurfaceContainerVariant,
                            )
                        }
                    }
                }
            }

            item("runtime") {
                Row(
                    modifier = Modifier.padding(start = 12.dp, end = 12.dp, bottom = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    MiuixTextButton(
                        text = stringResource(R.string.kpm_standalone_load),
                        onClick = actions.onLoadRuntime,
                    )
                    MiuixTextButton(
                        text = stringResource(R.string.kpm_doctor),
                        onClick = actions.onDoctor,
                    )
                }
            }
        }
    }
}

@Composable
private fun KpmItemMiuix(entry: KpmEntry, actions: KpmActions) {
    Card(
        modifier = Modifier
            .padding(horizontal = 12.dp)
            .padding(bottom = 12.dp),
        insideMargin = PaddingValues(16.dp),
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(modifier = Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = entry.name,
                            fontSize = 15.sp,
                            fontWeight = FontWeight.Medium,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = statusLabel(entry),
                            fontSize = 12.sp,
                            color = colorScheme.primary,
                        )
                    }
                    if (entry.version.isNotEmpty() || entry.author.isNotEmpty()) {
                        Text(
                            text = if (entry.author.isNotEmpty()) {
                                stringResource(R.string.kpm_version_author, entry.version, entry.author)
                            } else {
                                entry.version
                            },
                            fontSize = 12.sp,
                            color = colorScheme.onSurfaceContainerVariant,
                        )
                    }
                    if (entry.description.isNotEmpty()) {
                        Text(
                            text = entry.description,
                            fontSize = 12.sp,
                            color = colorScheme.onSurfaceContainerVariant,
                        )
                    }
                }
            }

            HorizontalDivider(modifier = Modifier.padding(vertical = 10.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                MiuixTextButton(
                    text = stringResource(R.string.kpm_control),
                    onClick = { actions.onControl(entry) },
                )
                MiuixTextButton(
                    text = stringResource(R.string.kpm_info),
                    onClick = { actions.onInfo(entry) },
                )
                if (entry.loaded) {
                    MiuixTextButton(
                        text = stringResource(R.string.kpm_unload),
                        onClick = { actions.onUnload(entry) },
                    )
                }
                if (entry.embedded) {
                    MiuixTextButton(
                        text = stringResource(R.string.kpm_remove),
                        onClick = { actions.onRemove(entry) },
                    )
                }
            }
        }
    }
}