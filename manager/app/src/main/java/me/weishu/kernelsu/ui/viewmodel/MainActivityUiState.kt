package me.weishu.kernelsu.ui.viewmodel

import androidx.compose.runtime.Immutable
import me.weishu.kernelsu.ui.UiMode
import me.weishu.kernelsu.ui.theme.AppSettings
import me.weishu.kernelsu.ui.util.BUILTIN_BACKGROUND_PATH

@Immutable
data class MainActivityUiState(
    val appSettings: AppSettings,
    val pageScale: Float,
    val enableBlur: Boolean,
    val enableFloatingBottomBar: Boolean,
    val enableFloatingBottomBarBlur: Boolean,
    val enableNavigationBadge: Boolean,
    val enableSwipeDismiss: Boolean,
    val pagerInterceptionMode: Int,
    val moduleDescriptionMaxLines: Int = 4,
    val uiMode: UiMode,
    val appBackgroundPath: String = BUILTIN_BACKGROUND_PATH,
    val appBackgroundDim: Float = 0.30f,
    val appCardDim: Float = 0.29f,
)
