package me.weishu.kernelsu.ui.screen.install

import android.content.Context
import android.net.Uri
import android.os.Parcelable
import android.provider.OpenableColumns
import androidx.annotation.StringRes
import kotlinx.parcelize.IgnoredOnParcel
import kotlinx.parcelize.Parcelize
import me.weishu.kernelsu.R

@Parcelize
internal sealed class InstallMethod : Parcelable {
    data class SelectFile(
        val uri: Uri? = null,
        @get:StringRes override val label: Int = R.string.select_file,
        override val summary: String?
    ) : InstallMethod() {
        override val writesPartition: Boolean
            get() = true
    }

    data class DownloadFile(
        val url: String? = null,
        val partition: String? = null,
        @get:StringRes override val label: Int = R.string.download_file,
        override val summary: String?
    ) : InstallMethod() {
        override val writesPartition: Boolean
            get() = true
    }

    data object DirectInstall : InstallMethod() {
        override val label: Int
            get() = R.string.direct_install

        override val writesPartition: Boolean
            get() = true

        // 越狱模式下唯一放行的写分区方式：它是「把内核补丁刷进当前设备」的正路，
        // 入口由两次「已解锁 BL」确认把关（首页卡片一次、开始刷写前一次）。
        override val allowedInJailbreak: Boolean
            get() = true
    }

    data object DirectInstallToInactiveSlot : InstallMethod() {
        override val label: Int
            get() = R.string.install_inactive_slot

        override val writesPartition: Boolean
            get() = true
    }

    abstract val label: Int

    @IgnoredOnParcel
    open val summary: String? = null

    /**
     * 该安装方式最终会写入 boot / init_boot 等分区。
     * 越狱模式（jailbreak / late-load）跑在已锁 BL 上，刷写分区会直接变砖，
     * 因此该模式下所有 writesPartition 的入口都必须在 UI 上置灰禁用。
     */
    @IgnoredOnParcel
    open val writesPartition: Boolean = false

    /**
     * 越狱模式下是否放行。
     *
     * 默认与 [writesPartition] 相反：该模式下写分区会变砖，因此一律置灰；
     * 只有「直接安装」例外，它承担的就是用户明确要求的刷写流程。
     */
    @IgnoredOnParcel
    open val allowedInJailbreak: Boolean
        get() = false

    /** 越狱模式下是否要拦下这一项（UI 置灰 + 点击无响应 + 刷写前兜底）。 */
    fun blockedInJailbreak(jailbreakMode: Boolean): Boolean =
        jailbreakMode && writesPartition && !allowedInJailbreak
}

fun isKoFile(context: Context, uri: Uri): Boolean {
    val seg = uri.lastPathSegment ?: ""
    if (seg.endsWith(".ko", ignoreCase = true)) return true

    return try {
        context.contentResolver.query(
            uri,
            arrayOf(OpenableColumns.DISPLAY_NAME),
            null,
            null,
            null
        )?.use { cursor ->
            val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (idx != -1 && cursor.moveToFirst()) {
                val name = cursor.getString(idx)
                name?.endsWith(".ko", ignoreCase = true) == true
            } else {
                false
            }
        } ?: false
    } catch (_: Throwable) {
        false
    }
}
