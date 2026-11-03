package me.weishu.kernelsu.ui.component

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.layout.ContentScale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.ui.util.hasAppBackground
import me.weishu.kernelsu.ui.util.loadAppBackgroundBitmap

/**
 * What the window layer is currently painting. Widgets that sample the content behind them
 * (the liquid glass bottom bar) need the picture too, otherwise they blur an empty layer and
 * look like a plain black slab.
 */
@Immutable
data class AppBackgroundInfo(
    val bitmap: ImageBitmap,
    val dim: Float,
)

val LocalAppBackgroundInfo = staticCompositionLocalOf<AppBackgroundInfo?> { null }

/**
 * Paints the user chosen image behind the whole app window (status bar, navigation bar and
 * every page included). Transparency of the pages themselves is handled by the theme, which
 * turns the surface color translucent while a background is active.
 *
 * The picture lives only here, on the window layer: pages stay fully transparent and never
 * paint their own copy, which is what keeps the wallpaper still while screens cross fade.
 */
@Composable
fun AppBackground(
    path: String,
    dim: Float,
    content: @Composable () -> Unit,
) {
    if (!hasAppBackground(path)) {
        content()
        return
    }

    val bitmap = rememberAppBackgroundBitmap(path)
    val veil = dim.coerceIn(0f, 0.95f)
    val info = bitmap?.let { AppBackgroundInfo(it, veil) }

    CompositionLocalProvider(LocalAppBackgroundInfo provides info) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(Color.Black)
        ) {
            bitmap?.let {
                Image(
                    bitmap = it,
                    contentDescription = null,
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Crop,
                )
                // The page darkness is painted exactly once here instead of being baked into every
                // surface color, which used to stack up into a much darker picture than requested.
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = veil))
                )
            }
            content()
        }
    }
}

/** Decode the picture once and share the same instance with every page surface. */
@Composable
fun rememberAppBackgroundBitmap(path: String): ImageBitmap? {
    if (!hasAppBackground(path)) return null
    val bitmap by produceState<ImageBitmap?>(initialValue = null, path) {
        value = withContext(Dispatchers.IO) { loadAppBackgroundBitmap(path) }
    }
    return bitmap
}
