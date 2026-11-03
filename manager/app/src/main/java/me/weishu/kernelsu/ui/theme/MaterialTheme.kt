package me.weishu.kernelsu.ui.theme

import android.app.Activity
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.core.view.WindowInsetsControllerCompat
import me.weishu.kernelsu.ui.webui.MonetColorsProvider

@Composable
fun MaterialKernelSUTheme(
    appSettings: AppSettings,
    backgroundDim: Float? = null,
    cardDim: Float? = null,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val systemDarkTheme = isSystemInDarkTheme()
    val darkTheme = appSettings.colorMode.isDark || (appSettings.colorMode.isSystem && systemDarkTheme)
    val amoledMode = appSettings.colorMode.isAmoled
    val dynamicColor = appSettings.keyColor == 0

    val colorScheme = rememberKernelSUColorScheme(
        seedColor = if (dynamicColor) Color.Unspecified else Color(appSettings.keyColor),
        isDark = darkTheme,
        isAmoled = amoledMode,
        paletteStyle = appSettings.paletteStyle,
        colorSpec = appSettings.colorSpec,
    )

    LaunchedEffect(darkTheme) {
        val window = (context as? Activity)?.window ?: return@LaunchedEffect
        WindowInsetsControllerCompat(window, window.decorView).apply {
            isAppearanceLightStatusBars = !darkTheme
            isAppearanceLightNavigationBars = !darkTheme
        }
    }

    val animatedColorScheme = colorScheme.animateAsState()
    val finalColorScheme = if (backgroundDim == null) {
        animatedColorScheme
    } else {
        // A custom background image is active: turn every page/container color into a
        // translucent black veil so the picture shows through the whole app.
        // Cards / sheets / bottom bars get their own darkness, the page backdrop is painted
        // once by AppBackground.
        val cardAlpha = (cardDim ?: backgroundDim).coerceIn(0f, 0.95f)
        val cardVeil = Color.Black.copy(alpha = cardAlpha)
        // Dark layers need light text to stay readable.
        val pageOn = if (backgroundDim >= 0.45f) Color.White else animatedColorScheme.onBackground
        val cardOn = if (cardAlpha >= 0.45f) Color.White else animatedColorScheme.onSurface
        animatedColorScheme.copy(
            // AppBackground paints the page veil once; page containers stay transparent.
            background = Color.Transparent,
            onBackground = pageOn,
            surface = cardVeil,
            onSurface = cardOn,
            onSurfaceVariant = cardOn.copy(alpha = 0.8f),
            surfaceContainerLowest = Color.Transparent,
            surfaceContainerLow = cardVeil,
            surfaceContainer = Color.Transparent,
            surfaceContainerHigh = cardVeil,
            surfaceContainerHighest = cardVeil,
        )
    }

    MaterialExpressiveTheme(
        colorScheme = finalColorScheme,
        motionScheme = MotionScheme.expressive(),
        typography = Typography,
        content = {
            MonetColorsProvider.UpdateCss(colorScheme)
            content()
        }
    )
}
