package me.weishu.kernelsu.ui.screen.colorpalette

import android.graphics.Bitmap
import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.R
import me.weishu.kernelsu.ui.navigation3.LocalNavigator
import me.weishu.kernelsu.ui.util.decodeAppBackgroundSource
import me.weishu.kernelsu.ui.viewmodel.SettingsViewModel
import top.yukonga.miuix.kmp.basic.ButtonDefaults
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Back
import top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme
import kotlin.math.roundToInt

/** Geometry of the crop frame, expressed in pixels of the current screen. */
private data class CropGeometry(val widthPx: Float, val heightPx: Float, val baseScale: Float)

/**
 * Crop screen shown after the user picked a picture for the app background.
 *
 * The frame always has the aspect ratio of the display, so the saved picture can be drawn with
 * [androidx.compose.ui.layout.ContentScale.Crop] without any further cropping and without black
 * bars. The user can pan and pinch the picture inside the frame, only the visible part is kept.
 */
@Composable
fun BackgroundCropScreen(uri: String) {
    val navigator = LocalNavigator.current
    val context = LocalContext.current
    val viewModel = viewModel<SettingsViewModel>()
    val density = LocalDensity.current

    val screenRatio = remember {
        val metrics = context.resources.displayMetrics
        if (metrics.widthPixels > 0 && metrics.heightPixels > 0) {
            metrics.widthPixels.toFloat() / metrics.heightPixels
        } else {
            9f / 20f
        }
    }

    var source by remember { mutableStateOf<Bitmap?>(null) }
    var failed by remember { mutableStateOf(false) }
    var zoom by remember { mutableFloatStateOf(1f) }
    var pan by remember { mutableStateOf(Offset.Zero) }
    var geometry by remember { mutableStateOf<CropGeometry?>(null) }

    LaunchedEffect(uri) {
        val bitmap = withContext(Dispatchers.IO) {
            decodeAppBackgroundSource(context, Uri.parse(uri))
        }
        source = bitmap
        failed = bitmap == null
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.settings_custom_background_crop_title),
                navigationIcon = {
                    IconButton(onClick = dropUnlessResumed { navigator.pop() }) {
                        Icon(MiuixIcons.Back, contentDescription = null, tint = colorScheme.onBackground)
                    }
                },
                scrollBehavior = MiuixScrollBehavior(),
            )
        },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            BoxWithConstraints(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .padding(vertical = 12.dp),
                contentAlignment = Alignment.Center,
            ) {
                val availableWidth = constraints.maxWidth.toFloat()
                val availableHeight = constraints.maxHeight.toFloat()
                var frameWidth = availableWidth
                var frameHeight = frameWidth / screenRatio
                if (frameHeight > availableHeight) {
                    frameHeight = availableHeight
                    frameWidth = frameHeight * screenRatio
                }
                val bitmap = source
                val baseScale = if (bitmap != null) {
                    maxOf(frameWidth / bitmap.width, frameHeight / bitmap.height)
                } else {
                    1f
                }
                SideEffect {
                    val updated = CropGeometry(frameWidth, frameHeight, baseScale)
                    if (geometry != updated) geometry = updated
                }
                Box(
                    modifier = Modifier
                        .size(
                            with(density) { frameWidth.toDp() },
                            with(density) { frameHeight.toDp() },
                        )
                        .clipToBounds()
                        .background(Color.Black),
                ) {
                    if (bitmap != null) {
                        val displayWidth = bitmap.width * baseScale
                        val displayHeight = bitmap.height * baseScale
                        Image(
                            bitmap = bitmap.asImageBitmap(),
                            contentDescription = null,
                            modifier = Modifier
                                .align(Alignment.Center)
                                .size(
                                    with(density) { displayWidth.toDp() },
                                    with(density) { displayHeight.toDp() },
                                )
                                .graphicsLayer {
                                    scaleX = zoom
                                    scaleY = zoom
                                    translationX = pan.x
                                    translationY = pan.y
                                },
                        )
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .pointerInput(bitmap, frameWidth, frameHeight) {
                                    detectTransformGestures { _, panChange, zoomChange, _ ->
                                        val newZoom = (zoom * zoomChange).coerceIn(1f, MAX_ZOOM)
                                        val scaledWidth = bitmap.width * baseScale * newZoom
                                        val scaledHeight = bitmap.height * baseScale * newZoom
                                        val maxX = ((scaledWidth - frameWidth) / 2f).coerceAtLeast(0f)
                                        val maxY = ((scaledHeight - frameHeight) / 2f).coerceAtLeast(0f)
                                        zoom = newZoom
                                        pan = Offset(
                                            (pan.x + panChange.x).coerceIn(-maxX, maxX),
                                            (pan.y + panChange.y).coerceIn(-maxY, maxY),
                                        )
                                    }
                                },
                        )
                    }
                }
            }
            Text(
                text = if (failed) {
                    stringResource(R.string.settings_custom_background_crop_failed)
                } else {
                    stringResource(R.string.settings_custom_background_crop_hint)
                },
                color = colorScheme.onSurfaceVariantSummary,
                textAlign = TextAlign.Center,
            )
            Spacer(Modifier.height(16.dp))
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                TextButton(
                    text = stringResource(android.R.string.cancel),
                    onClick = dropUnlessResumed { navigator.pop() },
                    modifier = Modifier.weight(1f),
                )
                TextButton(
                    text = stringResource(R.string.settings_custom_background_crop_apply),
                    onClick = {
                        val bitmap = source
                        val frame = geometry
                        if (bitmap == null || frame == null) return@TextButton
                        val cropped = runCatching { cropToFrame(bitmap, frame, zoom, pan) }.getOrNull()
                        if (cropped == null) return@TextButton
                        viewModel.setAppBackgroundFromBitmap(cropped)
                        navigator.pop()
                    },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.textButtonColorsPrimary(),
                    enabled = source != null,
                )
            }
        }
    }
}

private const val MAX_ZOOM = 4f

/**
 * Slices the visible part of the framed picture.
 *
 * [CropGeometry.baseScale] scales the bitmap to cover the frame, [zoom] and [pan] are the user
 * adjustments on top of that.
 */
private fun cropToFrame(
    bitmap: Bitmap,
    geometry: CropGeometry,
    zoom: Float,
    pan: Offset,
): Bitmap {
    val scale = geometry.baseScale * zoom
    val displayWidth = bitmap.width * scale
    val displayHeight = bitmap.height * scale
    val left = (geometry.widthPx - displayWidth) / 2f + pan.x
    val top = (geometry.heightPx - displayHeight) / 2f + pan.y
    val x = ((-left) / scale).roundToInt().coerceIn(0, bitmap.width - 1)
    val y = ((-top) / scale).roundToInt().coerceIn(0, bitmap.height - 1)
    val width = (geometry.widthPx / scale).roundToInt().coerceIn(1, bitmap.width - x)
    val height = (geometry.heightPx / scale).roundToInt().coerceIn(1, bitmap.height - y)
    return Bitmap.createBitmap(bitmap, x, y, width, height)
}
