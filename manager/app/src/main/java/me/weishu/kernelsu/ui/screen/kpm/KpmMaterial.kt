package me.weishu.kernelsu.ui.screen.kpm

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SmallExtendedFloatingActionButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.component.material.ExpressiveScaffold
import me.weishu.kernelsu.ui.component.material.TonalCard
import me.weishu.kernelsu.ui.component.material.expressiveTopAppBarColors

/**
 * KPM 页的 Material 版。骨架照抄模块页：同样的 `ExpressiveScaffold` + 大标题栏 +
 * 右下角 `SmallExtendedFloatingActionButton`，列表也是 `LazyColumn` + `TonalCard`，
 * 间距 13.dp、左右 16.dp —— 这样在两个底部标签之间来回切时不会有"换了个 App"的感觉。
 */
@Composable
internal fun KpmPagerMaterial(
    uiState: KpmUiStateHolder,
    actions: KpmActions,
    bottomInnerPadding: Dp,
) {
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    val visible = uiState.entries.filter {
        uiState.search.isBlank() ||
            it.name.contains(uiState.search, true) ||
            it.id.contains(uiState.search, true)
    }
    val addLabel = stringResource(R.string.kpm_add)

    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.kpm_page_title)) },
                actions = {
                    IconButton(onClick = actions.onRefresh) {
                        Icon(
                            imageVector = Icons.Outlined.Refresh,
                            contentDescription = stringResource(R.string.kpm_refresh),
                        )
                    }
                },
                colors = expressiveTopAppBarColors(),
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                scrollBehavior = scrollBehavior,
            )
        },
        floatingActionButton = {
            SmallExtendedFloatingActionButton(
                modifier = Modifier.padding(bottom = bottomInnerPadding),
                expanded = true,
                onClick = actions.onAdd,
                icon = { Icon(Icons.Filled.Add, addLabel) },
                text = { Text(addLabel) },
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            verticalArrangement = Arrangement.spacedBy(13.dp),
            contentPadding = PaddingValues(
                start = 16.dp,
                end = 16.dp,
                top = 4.dp,
                bottom = 16.dp + bottomInnerPadding + 56.dp + 16.dp,
            ),
        ) {
            item("store") {
                Text(
                    text = stringResource(R.string.kpm_store_hint, "/data/adb/kpm"),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            uiState.unavailableReason?.let { reason ->
                item("unavailable") {
                    TonalCard(containerColor = MaterialTheme.colorScheme.errorContainer) {
                        Text(
                            text = reason,
                            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onErrorContainer,
                        )
                    }
                }
            }

            if (uiState.entries.isEmpty()) {
                item("empty") {
                    Column {
                        Text(
                            text = stringResource(R.string.kpm_empty),
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(vertical = 24.dp),
                        )
                        // ksud 的原话：空列表是"没装"还是"答不上来"。
                        if (uiState.installedText.isNotBlank() && uiState.installedText != uiState.resultText) {
                            Text(
                                text = uiState.installedText,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            } else {
                items(visible, key = { it.id }) { entry ->
                    KpmItemMaterial(entry, actions)
                }
            }

            if (uiState.resultText.isNotEmpty()) {
                item("result") {
                    TonalCard {
                        Column(
                            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(6.dp),
                        ) {
                            Text(
                                text = stringResource(R.string.kpm_last_result),
                                style = MaterialTheme.typography.titleSmall,
                            )
                            Text(text = uiState.resultText, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }

            item("runtime") {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = actions.onLoadRuntime) {
                        Text(stringResource(R.string.kpm_standalone_load))
                    }
                    TextButton(onClick = actions.onDoctor) {
                        Text(stringResource(R.string.kpm_doctor))
                    }
                }
            }
        }
    }
}

@Composable
private fun KpmItemMaterial(entry: KpmEntry, actions: KpmActions) {
    TonalCard {
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
                        text = statusLabel(entry),
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
                TextButton(onClick = { actions.onControl(entry) }) {
                    Text(stringResource(R.string.kpm_control))
                }
                TextButton(onClick = { actions.onInfo(entry) }) {
                    Text(stringResource(R.string.kpm_info))
                }
                if (entry.loaded) {
                    TextButton(onClick = { actions.onUnload(entry) }) {
                        Text(stringResource(R.string.kpm_unload))
                    }
                }
                if (entry.embedded) {
                    TextButton(onClick = { actions.onRemove(entry) }) {
                        Text(stringResource(R.string.kpm_remove))
                    }
                }
            }
        }
    }
}

@Composable
internal fun statusLabel(entry: KpmEntry): String = when {
    !entry.embedded -> stringResource(R.string.kpm_status_loaded_only)
    entry.loaded -> stringResource(R.string.kpm_status_embedded_loaded)
    else -> stringResource(R.string.kpm_status_embedded)
}