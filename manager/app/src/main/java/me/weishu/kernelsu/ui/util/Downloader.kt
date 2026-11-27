package me.weishu.kernelsu.ui.util

import android.net.Uri
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.withContext
import me.weishu.kernelsu.data.registry.ExpectedFile
import me.weishu.kernelsu.ksuApp
import me.weishu.kernelsu.ui.util.module.LatestVersionInfo
import okhttp3.Request

/**
 * @author weishu
 * @date 2023/6/22.
 */
suspend fun download(
    url: String,
    fileName: String,
    onDownloaded: (Uri) -> Unit = {},
    onDownloading: () -> Unit = {},
    onProgress: (Int) -> Unit = {},
    expected: ExpectedFile? = null,
    onFailed: (DownloadManager.DownloadState) -> Unit = {},
) {
    onDownloading()

    val downloadId = DownloadManager.enqueue(
        context = ksuApp,
        url = url,
        fileName = fileName,
        onCompleted = onDownloaded,
        expected = expected,
    )

    val finalState = DownloadManager.downloads
        .onEach { map -> map[downloadId]?.let { onProgress(it.progress) } }
        .first { map ->
            val status = map[downloadId]?.status
            status == DownloadManager.Status.COMPLETED ||
                status == DownloadManager.Status.FAILED
        }[downloadId]
    if (finalState?.status == DownloadManager.Status.FAILED) {
        withContext(Dispatchers.Main) { onFailed(finalState) }
    }
}

internal suspend fun isDownloadAvailable(uri: Uri): Boolean = withContext(Dispatchers.IO) {
    runCatching {
        ksuApp.contentResolver.openFileDescriptor(uri, "r").use { it != null }
    }.getOrDefault(false)
}

/**
 * 自有版本清单地址（方案 B：不再请求 tiann/KernelSU 的 GitHub releases）。
 *
 * 期望返回的 JSON：
 * {
 *   "versionCode": 33002,
 *   "versionName": "v1.0.1",
 *   "downloadUrl": "https://mingri.xyz/mrsu/MRSU_1.0.9.3_33028-release.apk",
 *   "changelog": "..."
 * }
 *
 * versionCode 必须与发布 APK 的实际 versionCode 一致，否则会出现误报/漏报。
 */
private const val UPDATE_MANIFEST_URL = "https://mingri.xyz/mrsu/latest.json"

private const val UPDATE_TAG = "MRSU-Update"

fun checkNewVersion(): LatestVersionInfo {
    val defaultValue = LatestVersionInfo()

    // 这里不能拿 isNetworkAvailable() 当闸门：它要求 NET_CAPABILITY_VALIDATED，
    // 而部分设备/模拟器（以及飞行模式刚恢复的短暂窗口）拿不到这个能力，会被误判成没网。
    // 公告那条路径早就因为同样的原因绕开了它（见 Announcement.kt），这里保持一致：
    // 直接发请求，失败就静默返回空结果。
    return runCatching {
        ksuApp.okhttpClient.newCall(
            Request.Builder()
                .url(UPDATE_MANIFEST_URL)
                // 共享 OkHttp 客户端带 10MB 磁盘缓存；清单表示"当前最新版本"，
                // 一旦命中缓存就会一直读到旧值，表现为"服务器发布了，管理器却看不到"。
                .header("Cache-Control", "no-cache")
                .build()
        ).execute().use { response ->
            Log.d(UPDATE_TAG, "manifest http ${response.code}, cacheHit=${response.cacheResponse != null}")
            if (!response.isSuccessful) {
                return defaultValue
            }
            val body = response.body.string()
            Log.d(UPDATE_TAG, "manifest body=${body.take(200)}")
            parseVersionManifest(body) ?: defaultValue.also {
                Log.w(UPDATE_TAG, "manifest cannot be parsed")
            }
        }
    }.onFailure { Log.w(UPDATE_TAG, "manifest fetch failed", it) }.getOrDefault(defaultValue)
}

/** 解析版本清单；无法识别时返回 null。 */
private fun parseVersionManifest(body: String): LatestVersionInfo? {
    val json = runCatching { org.json.JSONObject(body) }.getOrNull() ?: return null

    // 自有清单格式
    val ownVersionCode = json.optInt("versionCode", 0)
    if (ownVersionCode > 0) {
        return LatestVersionInfo(
            versionCode = ownVersionCode,
            downloadUrl = json.optString("downloadUrl"),
            changelog = json.optString("changelog").ifBlank { json.optString("body") }
        )
    }

    // 兼容 GitHub releases 格式（保留以便切换到自建 releases 镜像）
    val assets = json.optJSONArray("assets") ?: return null
    for (i in 0 until assets.length()) {
        val asset = assets.optJSONObject(i) ?: continue
        val name = asset.optString("name")
        if (!name.endsWith(".apk")) continue

        val regex = Regex("v?(.+?)_(\\d+)-")
        val matchResult = regex.find(name) ?: continue
        val versionCode = matchResult.groupValues[2].toIntOrNull() ?: continue
        return LatestVersionInfo(
            versionCode = versionCode,
            downloadUrl = asset.optString("browser_download_url"),
            changelog = json.optString("body")
        )
    }
    return null
}
