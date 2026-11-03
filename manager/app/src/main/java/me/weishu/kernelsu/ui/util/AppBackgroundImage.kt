package me.weishu.kernelsu.ui.util

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.ksuApp
import java.io.File
import java.io.FileOutputStream

private const val BACKGROUND_DIR = "theme"
private const val BACKGROUND_FILE = "app_background.jpg"
private const val MAX_EDGE = 2048
private const val JPEG_QUALITY = 92

/**
 * MRSU: the stock wallpaper that ships inside the APK. It is referenced by an "asset://" path
 * instead of being copied into the profile, so it costs no user storage and "remove background"
 * stays available (the asset itself can never be deleted).
 */
const val ASSET_BACKGROUND_PREFIX = "asset://"
const val BUILTIN_BACKGROUND_PATH = ASSET_BACKGROUND_PREFIX + "default_background.jpg"

/** Where the imported background image is kept inside the app private storage. */
fun appBackgroundDir(): File {
    val dir = File(ksuApp.filesDir, BACKGROUND_DIR)
    if (!dir.exists()) dir.mkdirs()
    return dir
}

fun appBackgroundFile(): File = File(appBackgroundDir(), BACKGROUND_FILE)

fun hasAppBackground(path: String): Boolean = when {
    path.isBlank() -> false
    path.startsWith(ASSET_BACKGROUND_PREFIX) -> runCatching {
        ksuApp.assets.open(path.removePrefix(ASSET_BACKGROUND_PREFIX)).use { true }
    }.getOrDefault(false)
    else -> runCatching { File(path).let { it.exists() && it.length() > 0 } }.getOrDefault(false)
}

/** Decode the background with a sampled size so that very large pictures do not blow up the heap. */
fun loadAppBackgroundBitmap(path: String): ImageBitmap? {
    if (!hasAppBackground(path)) return null
    if (path.startsWith(ASSET_BACKGROUND_PREFIX)) {
        val name = path.removePrefix(ASSET_BACKGROUND_PREFIX)
        return runCatching {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            ksuApp.assets.open(name).use { BitmapFactory.decodeStream(it, null, bounds) }
            val options = BitmapFactory.Options().apply {
                inSampleSize = calculateInSampleSize(bounds.outWidth, bounds.outHeight, MAX_EDGE)
                inPreferredConfig = Bitmap.Config.RGB_565
            }
            ksuApp.assets.open(name).use { BitmapFactory.decodeStream(it, null, options) }?.asImageBitmap()
        }.getOrNull()
    }
    return runCatching {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(path, bounds)
        val options = BitmapFactory.Options().apply {
            inSampleSize = calculateInSampleSize(bounds.outWidth, bounds.outHeight, MAX_EDGE)
            inPreferredConfig = Bitmap.Config.RGB_565
        }
        BitmapFactory.decodeFile(path, options)?.asImageBitmap()
    }.getOrNull()
}

/** Copy the picked picture into private storage (down-scaled / re-encoded) and return the new path. */
suspend fun importAppBackground(context: Context, uri: Uri): String? = withContext(Dispatchers.IO) {
    runCatching {
        val resolver = context.contentResolver
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return@runCatching null

        val options = BitmapFactory.Options().apply {
            inSampleSize = calculateInSampleSize(bounds.outWidth, bounds.outHeight, MAX_EDGE)
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        val decoded = resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
            ?: return@runCatching null
        val scaled = scaleDown(decoded, MAX_EDGE)
        if (scaled !== decoded) decoded.recycle()

        // Use a fresh file name on every import so the UI reliably reloads the new picture
        // even when the previous background was stored under the same path.
        val dir = appBackgroundDir()
        val target = File(dir, "app_background_${System.currentTimeMillis()}.jpg")
        FileOutputStream(target).use { out ->
            scaled.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, out)
            out.flush()
        }
        scaled.recycle()
        // Drop leftovers from earlier imports (including the legacy fixed file name).
        dir.listFiles()?.forEach { file ->
            if (file.isFile && file.name != target.name) runCatching { file.delete() }
        }
        target.absolutePath
    }.getOrNull()
}

/**
 * Store an already cropped picture (see the crop screen) as the new background and return its path.
 * Runs on the calling thread (a worker thread), the caller is responsible for the dispatch.
 */
fun saveAppBackgroundBitmap(bitmap: Bitmap): String? = runCatching {
    val scaled = scaleDown(bitmap, MAX_EDGE)
    if (scaled !== bitmap) bitmap.recycle()
    val dir = appBackgroundDir()
    val target = File(dir, "app_background_${System.currentTimeMillis()}.jpg")
    FileOutputStream(target).use { out ->
        scaled.compress(Bitmap.CompressFormat.JPEG, JPEG_QUALITY, out)
        out.flush()
    }
    scaled.recycle()
    dir.listFiles()?.forEach { file ->
        if (file.isFile && file.name != target.name) runCatching { file.delete() }
    }
    target.absolutePath
}.getOrNull()

/** Decode a picked picture, down-sampled so that the crop screen works on a small bitmap. */
fun decodeAppBackgroundSource(context: Context, uri: Uri, maxEdge: Int = MAX_EDGE): Bitmap? =
    runCatching {
        val resolver = context.contentResolver
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return@runCatching null
        val options = BitmapFactory.Options().apply {
            inSampleSize = calculateInSampleSize(bounds.outWidth, bounds.outHeight, maxEdge)
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
    }.getOrNull()

fun clearAppBackground(path: String) {
    if (path.isBlank()) return
    runCatching { File(path).takeIf { it.exists() }?.delete() }
}

private fun calculateInSampleSize(width: Int, height: Int, maxEdge: Int): Int {
    var sample = 1
    var longest = maxOf(width, height)
    while (longest / 2 >= maxEdge) {
        longest /= 2
        sample *= 2
    }
    return sample
}

private fun scaleDown(bitmap: Bitmap, maxEdge: Int): Bitmap {
    val longest = maxOf(bitmap.width, bitmap.height)
    if (longest <= maxEdge) return bitmap
    val ratio = maxEdge.toFloat() / longest
    val width = (bitmap.width * ratio).toInt().coerceAtLeast(1)
    val height = (bitmap.height * ratio).toInt().coerceAtLeast(1)
    return Bitmap.createScaledBitmap(bitmap, width, height, true)
}
