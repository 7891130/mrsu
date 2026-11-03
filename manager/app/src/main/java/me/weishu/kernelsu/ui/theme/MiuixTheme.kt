package me.weishu.kernelsu.ui.theme

import android.app.Activity
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.core.view.WindowInsetsControllerCompat
import com.materialkolor.dynamiccolor.ColorSpec
import me.weishu.kernelsu.ui.webui.MonetColorsProvider
import top.yukonga.miuix.kmp.theme.ColorSchemeMode
import top.yukonga.miuix.kmp.theme.LocalContentColor
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.ThemeColorSpec
import top.yukonga.miuix.kmp.theme.ThemeController
import top.yukonga.miuix.kmp.theme.ThemePaletteStyle

@Composable
fun MiuixKernelSUTheme(
    appSettings: AppSettings,
    backgroundDim: Float? = null,
    cardDim: Float? = null,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val systemDarkTheme = isSystemInDarkTheme()
    val darkTheme = appSettings.colorMode.isDark || (appSettings.colorMode.isSystem && systemDarkTheme)
    val colorStyle = appSettings.paletteStyle
    val colorSpec = appSettings.colorSpec

    val miuixPaletteStyle = try {
        ThemePaletteStyle.valueOf(colorStyle.name)
    } catch (_: Exception) {
        ThemePaletteStyle.TonalSpot
    }

    val miuixColorSpec = if (colorSpec.effectiveFor(colorStyle) == ColorSpec.SpecVersion.SPEC_2025) {
        ThemeColorSpec.Spec2025
    } else {
        ThemeColorSpec.Spec2021
    }

    val resolvedKeyColor: Color? = when {
        appSettings.keyColor != 0 -> Color(appSettings.keyColor)
        appSettings.colorMode.isMonet ->
            if (darkTheme) dynamicDarkColorScheme(context).primary
            else dynamicLightColorScheme(context).primary

        else -> null
    }

    val controller = ThemeController(
        when (appSettings.colorMode) {
            ColorMode.SYSTEM -> ColorSchemeMode.System
            ColorMode.LIGHT -> ColorSchemeMode.Light
            ColorMode.DARK -> ColorSchemeMode.Dark
            ColorMode.MONET_SYSTEM -> ColorSchemeMode.MonetSystem
            ColorMode.MONET_LIGHT -> ColorSchemeMode.MonetLight
            ColorMode.MONET_DARK, ColorMode.DARK_AMOLED -> ColorSchemeMode.MonetDark
        },
        keyColor = resolvedKeyColor,
        isDark = darkTheme,
        paletteStyle = miuixPaletteStyle,
        colorSpec = miuixColorSpec,
    )

    MiuixTheme(
        controller = controller,
        content = {
            LaunchedEffect(darkTheme) {
                val window = (context as? Activity)?.window ?: return@LaunchedEffect
                WindowInsetsControllerCompat(window, window.decorView).apply {
                    isAppearanceLightStatusBars = !darkTheme
                    isAppearanceLightNavigationBars = !darkTheme
                }
            }
            MonetColorsProvider.UpdateCss()
            if (backgroundDim == null) {
                CompositionLocalProvider(
                    LocalContentColor provides MiuixTheme.colorScheme.onBackground,
                ) {
                    content()
                }
            } else {
                // A custom background image is active: let the page surface become a partly
                // transparent black veil so the picture shows through every screen.
                // Cards and sheets can be tuned separately from the page backdrop so the
                // picture stays readable behind lists, dialogs and preference cards.
                val cardAlpha = (cardDim ?: backgroundDim).coerceIn(0f, 0.95f)
                val cardVeil = Color.Black.copy(alpha = cardAlpha)
                // Once a layer gets dark the default dark-on-light text becomes unreadable, so
                // the matching "on" colors flip to light variants.
                val pageOn = if (backgroundDim >= 0.45f) Color.White else MiuixTheme.colorScheme.onBackground
                val cardOn = if (cardAlpha >= 0.45f) Color.White else MiuixTheme.colorScheme.onSurfaceContainer
                val translucentScheme = MiuixTheme.colorScheme.copy(
                    // The page darkness is painted once by AppBackground, so pages themselves
                    // stay fully transparent and the picture is never double dimmed.
                    background = Color.Transparent,
                    surface = Color.Transparent,
                    onBackground = pageOn,
                    onBackgroundVariant = pageOn.copy(alpha = 0.8f),
                    onSurface = pageOn,
                    onSurfaceSecondary = pageOn.copy(alpha = 0.8f),
                    onSurfaceVariantSummary = pageOn.copy(alpha = 0.75f),
                    onSurfaceVariantActions = pageOn,
                    // Cards / sheets / preference blocks use the container tokens and get
                    // their own, independently adjustable darkness.
                    surfaceContainer = cardVeil,
                    surfaceContainerHigh = cardVeil,
                    surfaceContainerHighest = cardVeil,
                    onSurfaceContainer = cardOn,
                    onSurfaceContainerVariant = cardOn.copy(alpha = 0.8f),
                    onSurfaceContainerHigh = cardOn,
                    onSurfaceContainerHighest = cardOn,
                )
                MiuixTheme(
                    colors = translucentScheme,
                    textStyles = MiuixTheme.textStyles,
                ) {
                    CompositionLocalProvider(
                        LocalContentColor provides translucentScheme.onBackground,
                        // Hand the same palette to widgets that Miuix renders in its own popup
                        // host, where `MiuixTheme.colorScheme` falls back to the opaque default.
                        LocalAppColorScheme provides translucentScheme,
                    ) {
                        content()
                    }
                }
            }
        }
    )
}
