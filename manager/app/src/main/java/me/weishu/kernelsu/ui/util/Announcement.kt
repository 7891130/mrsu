package me.weishu.kernelsu.ui.util

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import me.weishu.kernelsu.ksuApp
import okhttp3.Request
import org.json.JSONObject

/**
 * MRSU 后台公告（与 /mrsu/latest.json 同源的静态 JSON）。
 *
 * 期望返回的 JSON：
 * {
 *   "id": 3,
 *   "title": "公告标题",
 *   "content": "公告正文，可含换行",
 *   "level": 0,            // 0 普通 1 重要 2 紧急
 *   "active": true,
 *   "updatedAt": "2026-10-30 18:22:28"
 * }
 *
 * 后台同一时间只对外输出一条已上线公告；无公告时 id 为 0、active 为 false。
 */
private const val ANNOUNCEMENT_URL = "https://mingri.xyz/mrsu/announcement.json"
private const val ANNOUNCEMENT_TAG = "MRSU-Announcement"

private const val PREF_NAME = "mrsu_announcement"
private const val KEY_LAST_SHOWN_ID = "last_shown_id"

data class Announcement(
    val id: Long = 0L,
    val title: String = "",
    val content: String = "",
    val level: Int = 0,
    val updatedAt: String = "",
) {
    val isValid: Boolean
        get() = id > 0L && (title.isNotBlank() || content.isNotBlank())

    /** 弹窗标题：标题为空时用正文首行兜底。 */
    val displayTitle: String
        get() = title.ifBlank { content.lineSequence().firstOrNull { it.isNotBlank() }?.take(30).orEmpty() }

    /** 首页卡片摘要：正文首行非空内容。 */
    val summary: String
        get() = content.lineSequence().firstOrNull { it.isNotBlank() }.orEmpty()
}

/** 拉取当前公告；网络不可用或解析失败时返回 null（公告失败不影响首页其它信息）。 */
fun checkAnnouncement(): Announcement? {
    // 不依赖 isNetworkAvailable：部分设备/模拟器的 VALIDATED 能力缺失会误判为无网络，
    // 这里直接发起请求，失败即静默返回 null。
    return runCatching {
        ksuApp.okhttpClient.newCall(Request.Builder().url(ANNOUNCEMENT_URL).build()).execute()
            .use { response ->
                if (!response.isSuccessful) {
                    Log.d(ANNOUNCEMENT_TAG, "http ${response.code}")
                    return null
                }
                val body = response.body.string()
                Log.d(ANNOUNCEMENT_TAG, "http 200 body=${body.take(160)}")
                parseAnnouncement(body)
            }
    }.onFailure { Log.w(ANNOUNCEMENT_TAG, "fetch failed", it) }.getOrNull()
}

/** 解析公告 JSON；无有效公告（id<=0 / 已下线 / 内容为空）时返回 null。 */
private fun parseAnnouncement(body: String): Announcement? {
    val json = runCatching { JSONObject(body) }.getOrNull() ?: return null
    if (!json.optBoolean("active", false)) return null
    val id = json.optLong("id", 0L)
    if (id <= 0L) return null
    val announcement = Announcement(
        id = id,
        title = json.optString("title").trim(),
        content = json.optString("content").trim(),
        level = json.optInt("level", 0),
        updatedAt = json.optString("updatedAt"),
    )
    return announcement.takeIf { it.isValid }
}

private fun prefs(context: Context): SharedPreferences =
    context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

/** 已提示过的公告 id，用于避免同一条公告反复自动弹窗。 */
fun lastShownAnnouncementId(context: Context = ksuApp): Long =
    prefs(context).getLong(KEY_LAST_SHOWN_ID, 0L)

fun markAnnouncementShown(id: Long, context: Context = ksuApp) {
    if (id <= 0L) return
    prefs(context).edit().putLong(KEY_LAST_SHOWN_ID, id).apply()
}
