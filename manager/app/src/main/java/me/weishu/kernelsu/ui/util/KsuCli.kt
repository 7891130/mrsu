package me.weishu.kernelsu.ui.util

import android.content.ContentResolver
import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.os.Environment
import android.os.Parcelable
import android.os.SystemClock
import android.provider.MediaStore
import android.provider.OpenableColumns
import android.system.Os
import android.util.Log
import com.topjohnwu.superuser.CallbackList
import com.topjohnwu.superuser.Shell
import com.topjohnwu.superuser.ShellUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.parcelize.Parcelize
import me.weishu.kernelsu.BuildConfig
import me.weishu.kernelsu.Natives
import me.weishu.kernelsu.core.tasks.BootKernelVersion
import me.weishu.kernelsu.core.tasks.ExtractImage
import me.weishu.kernelsu.core.tasks.ProbeResult
import me.weishu.kernelsu.core.utils.DataSourceChannel
import me.weishu.kernelsu.data.registry.DownloadVerificationException
import me.weishu.kernelsu.data.registry.RegistryDownloadStore
import me.weishu.kernelsu.data.registry.copyForInstall
import me.weishu.kernelsu.ksuApp
import okhttp3.OkHttpClient
import org.json.JSONArray
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets
import java.util.concurrent.TimeUnit
import java.util.zip.ZipFile

/**
 * @author weishu
 * @date 2023/1/1.
 */
private const val TAG = "KsuCli"

private fun getKsuDaemonPath(): String {
    return ksuApp.applicationInfo.nativeLibraryDir + File.separator + "libksud.so"
}

/**
 * 修补 boot 的暂存目录。
 * 无论是否已安装，ksud 都先输出到这里，再由 [publishPatchedImages] 发布到公共下载目录。
 * 未安装时应用既无 root 也无 MANAGE_EXTERNAL_STORAGE，按路径直写 /sdcard 会被拒绝，
 * 因此不能直接把 -o 指向 Download。
 */
private fun patchOutputDir(): File {
    val dir = File(ksuApp.getExternalFilesDir(null) ?: ksuApp.filesDir, "patch")
    dir.mkdirs()
    // 清掉上一次的残留，避免把旧镜像当成本次产物发布
    dir.listFiles()?.forEach { runCatching { it.delete() } }
    return dir
}

/**
 * 把暂存目录里的修补镜像发布到公共下载目录，返回可展示的路径。
 * 走 MediaStore 写入 Download/，应用无需任何存储权限，用户也能在
 * 「文件管理 - 下载」里直接找到。
 */
private fun publishPatchedImages(dir: File, onStdout: (String) -> Unit) {
    val images = dir.listFiles()?.filter { it.isFile && it.name.endsWith(".img") }.orEmpty()
    if (images.isEmpty()) return
    images.forEach { img ->
        val publicPath = runCatching {
            val resolver = ksuApp.contentResolver
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, img.name)
                put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream")
                put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
            val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: error("insert failed")
            resolver.openOutputStream(uri)?.use { out ->
                img.inputStream().use { it.copyTo(out) }
            } ?: error("openOutputStream failed")
            values.clear()
            values.put(MediaStore.MediaColumns.IS_PENDING, 0)
            resolver.update(uri, values, null, null)
            "/storage/emulated/0/${Environment.DIRECTORY_DOWNLOADS}/${img.name}"
        }.getOrNull()
        if (publicPath != null) {
            img.delete()
            onStdout("- 修补结果已保存到 $publicPath")
        } else {
            onStdout("- 修补结果已保存到 ${img.absolutePath}")
        }
    }
}

data class FlashResult(val code: Int, val err: String, val showReboot: Boolean) {
    constructor(result: Shell.Result, showReboot: Boolean) : this(result.code, result.err.joinToString("\n"), showReboot)
    constructor(result: Shell.Result) : this(result, result.isSuccess)
}

object KsuCli {
    val SHELL: Shell = createRootShell()
    val GLOBAL_MNT_SHELL: Shell = createRootShell(true)
}

fun getRootShell(globalMnt: Boolean = false): Shell {
    return if (globalMnt) KsuCli.GLOBAL_MNT_SHELL else {
        KsuCli.SHELL
    }
}

inline fun <T> withNewRootShell(
    globalMnt: Boolean = false,
    block: Shell.() -> T
): T {
    return createRootShell(globalMnt).use(block)
}

fun Uri.getFileName(context: Context): String? {
    var fileName: String? = null
    val contentResolver: ContentResolver = context.contentResolver
    val cursor: Cursor? = contentResolver.query(this, null, null, null, null)
    cursor?.use {
        if (it.moveToFirst()) {
            fileName = it.getString(it.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
        }
    }
    return fileName
}

fun createRootShell(globalMnt: Boolean = false): Shell {
    Shell.enableVerboseLogging = BuildConfig.DEBUG
    val builder = Shell.Builder.create()
    return try {
        if (globalMnt) {
            builder.build(getKsuDaemonPath(), "debug", "su", "-g")
        } else {
            builder.build(getKsuDaemonPath(), "debug", "su")
        }
    } catch (e: Throwable) {
        Log.w(TAG, "ksu failed: ", e)
        try {
            if (globalMnt) {
                builder.build("su", "-mm")
            } else {
                builder.build("su")
            }
        } catch (e: Throwable) {
            Log.e(TAG, "su failed: ", e)
            builder.build("sh")
        }
    }
}

fun execKsud(args: String, newShell: Boolean = false, globalMnt: Boolean = false): Boolean {
    return if (newShell) {
        withNewRootShell(globalMnt = globalMnt) {
            ShellUtils.fastCmdResult(this, "${getKsuDaemonPath()} $args")
        }
    } else {
        ShellUtils.fastCmdResult(getRootShell(globalMnt), "${getKsuDaemonPath()} $args")
    }
}

suspend fun getFeatureStatus(feature: String): String = withContext(Dispatchers.IO) {
    val shell = getRootShell()
    val out = shell.newJob()
        .add("${getKsuDaemonPath()} feature check $feature").to(ArrayList<String>(), null).exec().out
    out.firstOrNull()?.trim().orEmpty()
}

suspend fun getFeaturePersistValue(feature: String): Long? = readFeatureValue("--config $feature")

// Current value in kernel
suspend fun getFeatureValue(feature: String): Long? = readFeatureValue(feature)

private suspend fun readFeatureValue(args: String): Long? = withContext(Dispatchers.IO) {
    val shell = getRootShell()
    val out = shell.newJob()
        .add("${getKsuDaemonPath()} feature get $args").to(ArrayList<String>(), null).exec().out
    val valueLine = out.firstOrNull { it.trim().startsWith("Value:") } ?: return@withContext null
    valueLine.substringAfter("Value:").trim().toLongOrNull()
}

fun install() {
    val start = SystemClock.elapsedRealtime()
    val libadbroot = File(ksuApp.applicationInfo.nativeLibraryDir, "libadbroot.so").absolutePath
    val result = execKsud("install --libadbroot $libadbroot --data-path ${ksuApp.applicationInfo.deviceProtectedDataDir}", true)
    Log.w(TAG, "install result: $result, cost: ${SystemClock.elapsedRealtime() - start}ms")
}

fun listModules(): String {
    val shell = getRootShell()

    val out = shell.newJob()
        .add("${getKsuDaemonPath()} module list").to(ArrayList(), null).exec().out
    return out.joinToString("\n").ifBlank { "[]" }
}

fun getModuleCount(): Int {
    val result = listModules()
    runCatching {
        val array = JSONArray(result)
        return array.length()
    }.getOrElse { return 0 }
}

fun getSuperuserCount(): Int {
    return Natives.getSuperuserCount()
}

fun toggleModule(id: String, enable: Boolean): Boolean {
    val cmd = if (enable) {
        "module enable $id"
    } else {
        "module disable $id"
    }
    val result = execKsud(cmd, true)
    Log.i(TAG, "$cmd result: $result")
    return result
}

fun undoUninstallModule(id: String): Boolean {
    val cmd = "module undo-uninstall $id"
    val result = execKsud(cmd, true)
    Log.i(TAG, "undo uninstall module $id result: $result")
    return result
}

fun uninstallModule(id: String): Boolean {
    val cmd = "module uninstall $id"
    val result = execKsud(cmd, true)
    Log.i(TAG, "uninstall module $id result: $result")
    return result
}

private fun flashWithIO(
    cmd: String,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit
): Shell.Result {

    val stdoutCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStdout(s ?: "")
        }
    }

    val stderrCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStderr(s ?: "")
        }
    }

    return withNewRootShell {
        newJob().add(cmd).to(stdoutCallback, stderrCallback).exec()
    }
}

fun flashModule(
    uri: Uri,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit
): FlashResult {
    // Registry downloads live in app-private storage and carry their expected size + SHA-256;
    // the exact copy handed to ksud is re-hashed right before installing.
    val storeRoot = File(ksuApp.filesDir, RegistryDownloadStore.DIR_NAME)
    val expected = if (uri.scheme == ContentResolver.SCHEME_FILE) {
        uri.path?.let { RegistryDownloadStore.expectedFor(storeRoot, File(it)) }
    } else {
        null
    }
    val file = File.createTempFile("module-", ".zip", ksuApp.cacheDir)
    try {
        try {
            val input = ksuApp.contentResolver.openInputStream(uri) ?: throw IOException("Cannot open $uri")
            input.use { copyForInstall(it, file, expected) }
        } catch (e: IOException) {
            Log.w("KernelSU", "module $uri not installed: ${e.message}")
            if (e is DownloadVerificationException) {
                // Corrupted or tampered after download: never keep it around for a retry.
                uri.path?.let { RegistryDownloadStore.delete(storeRoot, File(it)) }
            }
            val message = "! ${e.message ?: "Failed to read module"}"
            onStderr(message)
            return FlashResult(1, message, false)
        }
        val cmd = "module install ${file.absolutePath}"
        val result = flashWithIO("${getKsuDaemonPath()} $cmd", onStdout, onStderr)
        Log.i("KernelSU", "install module $uri result: $result")
        return FlashResult(result, result.isSuccess && moduleNeedsReboot(file))
    } finally {
        file.delete()
    }
}

private val MODULE_ID_REGEX = Regex("^[a-zA-Z][a-zA-Z0-9._-]+$")

// A hot installed module goes live without leaving the "update" flag behind
private fun moduleNeedsReboot(zip: File): Boolean {
    val id = runCatching {
        ZipFile(zip).use { archive ->
            val entry = archive.getEntry("module.prop") ?: return@use null
            archive.getInputStream(entry).bufferedReader().useLines { lines ->
                lines.map { it.trim() }.firstOrNull { it.startsWith("id=") }?.substringAfter('=')?.trim()
            }
        }
    }.getOrNull()
    if (id == null || !MODULE_ID_REGEX.matches(id)) return true
    val dir = "/data/adb/modules/$id"
    // Any failure to check counts as needing a reboot
    return !ShellUtils.fastCmdResult(getRootShell(), "[ -d $dir ] && [ ! -e $dir/update ]")
}

fun runModuleAction(
    moduleId: String, onStdout: (String) -> Unit, onStderr: (String) -> Unit
): Boolean {
    val stdoutCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStdout(s ?: "")
        }
    }

    val stderrCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStderr(s ?: "")
        }
    }

    val result = withNewRootShell(true) {
        newJob().add("${getKsuDaemonPath()} module action $moduleId")
            .to(stdoutCallback, stderrCallback).exec()
    }

    Log.i("KernelSU", "Module runAction result: $result")

    return result.isSuccess
}

/**
 * Runs a prepared one-shot script command (see ScriptCommand) in a new root shell in the
 * global mount namespace, like a module action. Returns the exit code.
 */
fun runOneshotScript(
    command: String, onStdout: (String) -> Unit, onStderr: (String) -> Unit
): Int {
    val stdoutCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStdout(s ?: "")
        }
    }

    val stderrCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStderr(s ?: "")
        }
    }

    val result = withNewRootShell(true) {
        newJob().add(command).to(stdoutCallback, stderrCallback).exec()
    }
    Log.i(TAG, "one-shot script exit code: ${result.code}")
    return result.code
}

/** Removes a script's extraction directory, including files the script created as root. */
fun removeAsRoot(path: String): Boolean {
    val escaped = "'" + path.replace("'", "'\\''") + "'"
    return ShellUtils.fastCmdResult(getRootShell(), "rm -rf $escaped")
}

fun restoreBoot(
    onStdout: (String) -> Unit, onStderr: (String) -> Unit
): FlashResult {
    val result = flashWithIO("${getKsuDaemonPath()} boot-restore -f", onStdout, onStderr)
    return FlashResult(result)
}

fun uninstallPermanently(
    onStdout: (String) -> Unit, onStderr: (String) -> Unit
): FlashResult {
    val result = flashWithIO("${getKsuDaemonPath()} uninstall --package-name ${BuildConfig.APPLICATION_ID}", onStdout, onStderr)
    return FlashResult(result)
}

@Parcelize
sealed class LkmSelection : Parcelable {
    @Parcelize
    data class LkmUri(val uri: Uri) : LkmSelection()

    @Parcelize
    data class KmiString(val value: String) : LkmSelection()

    @Parcelize
    data object KmiNone : LkmSelection()
}

private fun writeLkmFile(lkm: LkmSelection): File? {
    if (lkm !is LkmSelection.LkmUri) return null
    val file = File(ksuApp.cacheDir, "kernelsu-tmp-lkm.ko")
    ksuApp.contentResolver.openInputStream(lkm.uri)?.use { input ->
        file.outputStream().use { output -> input.copyTo(output) }
    }
    return file
}

private fun bootPatchFlags(
    allowShell: Boolean,
    enableAdb: Boolean,
    forceBackup: Boolean,
): String = buildString {
    if (allowShell) append(" --allow-shell")
    if (enableAdb) append(" --enable-adbd")
    if (forceBackup) append(" --backup")
}

fun installBoot(
    bootUri: Uri?,
    lkm: LkmSelection,
    ota: Boolean,
    partition: String?,
    allowShell: Boolean,
    enableAdb: Boolean,
    forceBackup: Boolean,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit,
): FlashResult {
    val resolver = ksuApp.contentResolver

    val bootFile = bootUri?.let { uri ->
        with(resolver.openInputStream(uri)) {
            val bootFile = File(ksuApp.cacheDir, "boot.img")
            bootFile.outputStream().use { output ->
                this?.copyTo(output)
            }

            bootFile
        }
    }

    // Direct install used to rely on `ksud boot-patch -f`, which lets ksud
    // unpack the live boot partition by itself. Boot partitions dumped from
    // some OEM devices (MTK/OPPO) carry a non-standard AVB footer, which
    // ksud's parser rejects ("invalid avb header"), so direct install failed
    // every single time on those devices. We now dump the partition by
    // ourselves, strip the AVB tail (vbmeta + footer) while keeping the boot
    // signature, patch that clean image, and flash the result back with dd.
    var directDev: String? = null
    var directRaw: File? = null
    var directImage: File? = null
    if (bootFile == null) {
        val dev = resolveBootPartition(partition, onStderr)
        if (dev == null) {
            return FlashResult(-1, "Unable to locate the current boot partition", false)
        }
        onStdout("- Reading current boot partition ($dev)")
        val rawDir = patchOutputDir().apply { mkdirs() }
        val raw = File(rawDir, "direct-raw.img")
        raw.delete()
        val tmpRaw = "/data/local/tmp/mrsu_boot_raw.img"
        val dump = getRootShell().newJob()
            .add("rm -f $tmpRaw")
            .add("dd if=$dev of=$tmpRaw bs=4096")
            .add("chmod 666 $tmpRaw")
            .add("cp -f $tmpRaw ${raw.absolutePath}")
            .add("chmod 666 ${raw.absolutePath}")
            .exec()
        if (!raw.exists() || raw.length() == 0L) {
            raw.delete()
            onStderr(dump.err.joinToString("\n"))
            return FlashResult(-1, "Failed to read the boot partition", false)
        }
        val clean = File(rawDir, "direct-boot.img")
        if (!stripAvbTail(raw, clean, onStdout)) {
            raw.delete()
            clean.delete()
            return FlashResult(-1, "Unsupported boot image layout", false)
        }
        onStdout("- Boot image prepared (${clean.length()} bytes)")
        // ksud runs as root and cannot read the app-private staging file, so the
        // trimmed image is rebuilt inside /data/local/tmp for the patch step.
        val tmpClean = "/data/local/tmp/mrsu_boot_clean.img"
        getRootShell().newJob()
            .add("rm -f $tmpClean")
            .add("head -c ${clean.length()} $tmpRaw > $tmpClean")
            .add("chmod 644 $tmpClean")
            .exec()
        directDev = dev
        directRaw = raw
        directImage = File(tmpClean)
    }

    val imageFile = bootFile ?: directImage

    var cmd = "boot-patch -b ${imageFile!!.absolutePath}"
    cmd += bootPatchFlags(allowShell, enableAdb, forceBackup)

    if (ota) {
        cmd += " -u"
    }

    val lkmFile = writeLkmFile(lkm)
    if (lkmFile != null) {
        cmd += " -m ${lkmFile.absolutePath}"
    } else if (lkm is LkmSelection.KmiString) {
        cmd += " --kmi ${lkm.value}"
    }

    val stagingDir = patchOutputDir()
    cmd += " -o ${stagingDir.absolutePath}"
    val existingImages = stagingDir.listFiles()?.map { it.absolutePath }?.toSet() ?: emptySet()

    partition?.let { part ->
        cmd += " --partition $part"
    }

    val result = flashWithIO("${getKsuDaemonPath()} $cmd", onStdout, onStderr)
    Log.i("KernelSU", "install boot result: ${result.isSuccess}")

    // Direct install patches a local copy, so the result has to be written
    // back to the partition manually (the old "-f" path did it inside ksud).
    var flashError: String? = null
    if (result.isSuccess && directDev != null) {
        val patched = stagingDir.listFiles()
            ?.filter {
                it.isFile && it.name.startsWith("kernelsu_patched") && it.absolutePath !in existingImages
            }
            ?.maxByOrNull { it.lastModified() }
        if (patched == null) {
            flashError = "Patched image not found in ${stagingDir.absolutePath}"
        } else {
            // Keep a copy of the stock image next to the patched one, so it
            // gets published to Download as a rollback point.
            directRaw?.let { raw ->
                runCatching {
                    raw.copyTo(File(stagingDir, "boot_backup_${System.currentTimeMillis()}.img"), true)
                }
            }
            // `su` runs in the ksu domain, which cannot write block devices by
            // default; grant the permission on the runtime policy first.
            runCatching {
                getRootShell().newJob()
                    .add(
                        "${getKsuDaemonPath()} sepolicy patch " +
                            "\"allow ksu block_device:blk_file { open write }\""
                    )
                    .exec()
            }
            onStdout("- Flashing patched image to $directDev")
            val write = getRootShell().newJob()
                .add("dd if=${patched.absolutePath} of=$directDev bs=4096 conv=fsync")
                .exec()
            if (write.isSuccess) {
                onStdout("- Flashed successfully")
            } else {
                onStderr(write.err.joinToString("\n"))
                flashError = "Failed to write the patched image back"
            }
        }
    }

    if (result.isSuccess) {
        publishPatchedImages(stagingDir, onStdout)
    }

    bootFile?.delete()
    directImage?.delete()
    directRaw?.delete()
    lkmFile?.delete()

    if (flashError != null && result.isSuccess) {
        return FlashResult(-1, flashError, false)
    }

    // if boot uri is empty, it is direct install, when success, we should show reboot button
    val showReboot = bootUri == null && result.isSuccess // we create a temporary val here, to avoid calc showReboot double
    if (showReboot) { // because we decide do not update ksud when startActivity
        install() // install ksud here
    }
    return FlashResult(result, showReboot)
}

private fun alignUp(value: Long, alignment: Long): Long =
    if (alignment <= 0L) value else (value + alignment - 1L) / alignment * alignment

/**
 * Resolves the block device of the partition that currently boots the device.
 */
private fun resolveBootPartition(partition: String?, onStderr: (String) -> Unit): String? {
    val slot = runCatching {
        getRootShell().newJob().add("getprop ro.boot.slot_suffix").exec()
            .out.joinToString("").trim()
    }.getOrDefault("")

    val names = LinkedHashSet<String>()
    if (!partition.isNullOrBlank()) {
        names += partition
        if (slot.isNotEmpty()) names += "$partition$slot"
    }
    if (slot.isNotEmpty()) names += "boot$slot"
    names += "boot_a"
    names += "boot"
    if (slot.isNotEmpty()) names += "init_boot$slot"
    names += "init_boot_a"
    names += "init_boot"

    for (name in names) {
        val path = "/dev/block/by-name/$name"
        val exists = runCatching {
            getRootShell().newJob()
                .add("test -e $path")
                .exec()
                .isSuccess
        }.getOrDefault(false)
        if (exists) return path
    }
    onStderr("No boot partition found under /dev/block/by-name")
    return null
}

/**
 * Copies only the real boot image out of a raw partition dump, dropping the
 * AVB trailer (vbmeta + footer) that some OEM partitions carry in a layout
 * ksud cannot parse. Header, kernel, ramdisk and the boot signature are kept.
 */
private fun stripAvbTail(raw: File, out: File, onStdout: (String) -> Unit): Boolean {
    try {
        RandomAccessFile(raw, "r").use { src ->
            val head = ByteArray(4096)
            if (src.read(head) < 1660) return false
            if (String(head, 0, 8, StandardCharsets.US_ASCII) != "ANDROID!") {
                onStdout("- Not an Android boot image")
                return false
            }
            val buf = ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN)
            val kernelSize = buf.getInt(8).toLong() and 0xFFFFFFFFL
            val version = buf.getInt(40)
            // v3/v4 moved ramdisk_size from offset 16 to offset 12.
            val ramdiskSize = if (version >= 3) {
                buf.getInt(12).toLong() and 0xFFFFFFFFL
            } else {
                buf.getInt(16).toLong() and 0xFFFFFFFFL
            }
            val pageSize = if (version >= 3) 4096L else (buf.getInt(36).toLong() and 0xFFFFFFFFL)
            if (pageSize <= 0L || pageSize > 65536L) return false
            val headerSize = if (version >= 3) {
                buf.getInt(20).toLong() and 0xFFFFFFFFL
            } else {
                pageSize
            }
            // v3/v4 header is capped at 4096 bytes; anything bigger is bogus.
            if (headerSize <= 0L || headerSize > 4096L) return false
            val signatureSize = if (version >= 4) buf.getInt(1580).toLong() and 0xFFFFFFFFL else 0L
            val kernelStart = alignUp(headerSize, pageSize)
            val ramdiskStart = alignUp(kernelStart + kernelSize, pageSize)
            val total = alignUp(ramdiskStart + ramdiskSize, pageSize) + signatureSize
            if (total <= 0L || total > src.length()) {
                onStdout("- Unexpected boot image size ($total / ${src.length()})")
                return false
            }
            src.seek(0)
            out.delete()
            var remaining = total
            FileOutputStream(out).use { dst ->
                val chunk = ByteArray(1 shl 20)
                while (remaining > 0L) {
                    val read = src.read(chunk, 0, minOf(remaining, chunk.size.toLong()).toInt())
                    if (read <= 0) break
                    dst.write(chunk, 0, read)
                    remaining -= read
                }
                dst.flush()
            }
            return remaining == 0L
        }
    } catch (e: Exception) {
        onStdout("- Failed to prepare the boot image: ${e.message}")
        return false
    }
}

fun downloadBoot(
    url: String,
    partition: String,
    lkm: LkmSelection,
    allowShell: Boolean,
    enableAdb: Boolean,
    forceBackup: Boolean,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit,
): FlashResult {
    val bootFile = File(ksuApp.cacheDir, "download-boot.img")
    var probedKmi: String? = null
    try {
        onStdout("- Downloading and extracting boot image")
        val channel = DataSourceChannel(newDownloadClient(), url)
        val magic = readMagic(channel)
        val image = ExtractImage(bootFile, onStdout)
        // Extract the KMI here while the payload is open. ZipFile closes the
        // channel it is built on, so probe on a separate channel.
        val probeChannel = DataSourceChannel(newDownloadClient(), url)
        probedKmi = try {
            if (magic == "CrAU") {
                ExtractImage.probePayload(
                    probeChannel,
                    withKmi = lkm is LkmSelection.KmiNone,
                    onProgress = onStdout,
                ).kmi
            } else {
                ExtractImage.probe(
                    probeChannel,
                    withKmi = lkm is LkmSelection.KmiNone,
                    onProgress = onStdout,
                ).kmi
            }
        } finally {
            probeChannel.close()
        }
        if (magic == "CrAU") {
            image.consumePayload(channel, partition)
        } else {
            image.consume(channel, partition)
        }
    } catch (e: Exception) {
        bootFile.delete()
        return FlashResult(-1, e.message ?: "Download failed", false)
    }

    // init_boot/vendor_boot carry no kernel, so their KMI comes from the
    // payload's boot probe and must be passed explicitly. A remote download
    // is unrelated to this device, so ksud must not use the local kernel.
    val autoKmi = if (lkm is LkmSelection.KmiNone) {
        (probedKmi ?: BootKernelVersion.parseKmiFromBoot(bootFile))?.also {
            onStdout("- Auto detected KMI: $it")
        }
    } else {
        null
    }
    if (autoKmi == null && lkm is LkmSelection.KmiNone) {
        bootFile.delete()
        return FlashResult(-1, "Failed to determine KMI from the package", false)
    }

    var cmd = "${getKsuDaemonPath()} boot-patch -b ${bootFile.absolutePath}"
    cmd += bootPatchFlags(allowShell, enableAdb, forceBackup)

    val lkmFile = writeLkmFile(lkm)
    if (lkmFile != null) {
        cmd += " -m ${lkmFile.absolutePath}"
    } else if (lkm is LkmSelection.KmiString) {
        cmd += " --kmi ${lkm.value}"
    }
    if (autoKmi != null) cmd += " --kmi $autoKmi"
    cmd += " --partition $partition"
    // ksud defaults to cwd, which is read-only; use an explicit staging dir.
    val stagingDir = patchOutputDir()
    cmd += " -o ${stagingDir.absolutePath}"

    val stdoutCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStdout(s ?: "")
        }
    }
    val stderrCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStderr(s ?: "")
        }
    }

    val result = Shell.getShell().newJob().add(cmd).to(stdoutCallback, stderrCallback).exec()
    lkmFile?.delete()
    bootFile.delete()
    if (result.isSuccess) {
        publishPatchedImages(stagingDir, onStdout)
    }
    return FlashResult(result, false)
}

suspend fun probeRemoteBootPartitions(url: String): ProbeResult = withContext(Dispatchers.IO) {
    Log.d(TAG, "probe start: $url")
    val channel = DataSourceChannel(newDownloadClient(), url)
    Log.d(TAG, "probe connected, size=${channel.size()}")
    val magic = readMagic(channel)
    Log.d(TAG, "probe magic: $magic")
    // Only list the partitions here; the KMI is extracted later when the
    // payload is downloaded for patching.
    val result = if (magic == "CrAU") {
        ExtractImage.probePayload(channel, withKmi = false)
    } else {
        ExtractImage.probe(channel, withKmi = false)
    }
    Log.d(TAG, "probe partitions: ${result.partitions}")
    result
}

private fun newDownloadClient(): OkHttpClient {
    return OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(20, TimeUnit.SECONDS)
        .writeTimeout(20, TimeUnit.SECONDS)
        .build()
}

private fun readMagic(channel: DataSourceChannel): String {
    val buffer = ByteBuffer.allocate(4)
    channel.read(buffer)
    channel.position(0)
    return String(buffer.array(), StandardCharsets.ISO_8859_1)
}

fun reboot(reason: String = "") {
    if (reason == "soft_reboot") {
        execKsud("soft-reboot", true, true)
        return
    }
    val shell = getRootShell()
    if (reason == "recovery") {
        // KEYCODE_POWER = 26, hide incorrect "Factory data reset" message
        ShellUtils.fastCmd(shell, "/system/bin/input keyevent 26")
    }
    ShellUtils.fastCmd(shell, "/system/bin/svc power reboot $reason || /system/bin/reboot $reason")
}

fun rootAvailable(): Boolean {
    val shell = getRootShell()
    return shell.isRoot
}

suspend fun getCurrentKmi(): String = withContext(Dispatchers.IO) {
    val shell = getRootShell()
    val cmd = "boot-info current-kmi"
    val fromKsud = runCatching {
        ShellUtils.fastCmd(shell, "${getKsuDaemonPath()} $cmd")
    }.getOrDefault("").trim()

    if (fromKsud.isNotBlank() && !fromKsud.equals("unknown", ignoreCase = true)) {
        return@withContext fromKsud
    }

    // ksud 不可用（无 root / daemon 缺失）时，退化为本地内核版本推断，
    // 例如 "5.10.236-android12-9-o" -> "android12-5.10"
    deriveKmiFromLocalKernel()
}

/** 从本机内核 release 字符串推断 KMI，失败返回空串。 */
private fun deriveKmiFromLocalKernel(): String {
    val release = runCatching { Os.uname().release }.getOrDefault("")
    if (release.isBlank()) return ""
    // "5.10.236-android12-9-o" -> android12-5.10
    Regex("""(\d+\.\d+)\S*?-(android\d+)""").find(release)?.let { match ->
        return "${match.groupValues[2]}-${match.groupValues[1]}"
    }
    // "6.1.157-android14-11-gki" 之类无分隔符的写法
    Regex("""(android\d+)-(\d+\.\d+)""").find(release)?.let { return it.value }
    val kv = me.weishu.kernelsu.parseKernelVersion(release)
    return when {
        kv.major > 5 -> "android${kv.major}-${kv.patchLevel}"
        kv.major == 5 && kv.patchLevel >= 10 -> "android12-${kv.patchLevel}"
        else -> ""
    }
}

suspend fun getSupportedKmis(): List<String> = withContext(Dispatchers.IO) {
    runCatching {
        val shell = getRootShell()
        val cmd = "boot-info supported-kmis"
        val out = shell.newJob().add("${getKsuDaemonPath()} $cmd").to(ArrayList(), null).exec().out
        out.filter { it.isNotBlank() }.map { it.trim() }
            // 过滤 ksud 的错误信息，只保留合法 KMI 名（如 android12-5.10）
            .filter { kmiRegex.matches(it) }
    }.getOrDefault(emptyList())
}

private val kmiRegex = Regex("""^android\d+-\d+\.\d+$""")

suspend fun isAbDevice(): Boolean = withContext(Dispatchers.IO) {
    val shell = getRootShell()
    val cmd = "boot-info is-ab-device"
    ShellUtils.fastCmd(shell, "${getKsuDaemonPath()} $cmd").trim().toBoolean()
}

suspend fun getDefaultPartition(): String = withContext(Dispatchers.IO) {
    val shell = getRootShell()
    if (shell.isRoot) {
        val cmd = "boot-info default-partition"
        ShellUtils.fastCmd(shell, "${getKsuDaemonPath()} $cmd").trim()
    } else {
        if (!Os.uname().release.contains("android12-")) "init_boot" else "boot"
    }
}

suspend fun getSlotSuffix(ota: Boolean): String = withContext(Dispatchers.IO) {
    val shell = getRootShell()
    val cmd = if (ota) {
        "boot-info slot-suffix --ota"
    } else {
        "boot-info slot-suffix"
    }
    ShellUtils.fastCmd(shell, "${getKsuDaemonPath()} $cmd").trim()
}

suspend fun getAvailablePartitions(): List<String> = withContext(Dispatchers.IO) {
    val shell = getRootShell()
    val cmd = "boot-info available-partitions"
    val out = shell.newJob().add("${getKsuDaemonPath()} $cmd").to(ArrayList(), null).exec().out
    out.filter { it.isNotBlank() }.map { it.trim() }
}

fun hasMagisk(): Boolean {
    val shell = getRootShell(true)
    val result = shell.newJob().add("which magisk").exec()
    Log.i(TAG, "has magisk: ${result.isSuccess}")
    return result.isSuccess
}

fun isSepolicyValid(rules: String?): Boolean {
    if (rules == null) {
        return true
    }
    val shell = getRootShell()
    val result =
        shell.newJob().add("${getKsuDaemonPath()} sepolicy check '$rules'").to(ArrayList(), null)
            .exec()
    return result.isSuccess
}

fun getSepolicy(pkg: String): String {
    val shell = getRootShell()
    val result =
        shell.newJob().add("${getKsuDaemonPath()} profile get-sepolicy $pkg").to(ArrayList(), null)
            .exec()
    Log.i(TAG, "code: ${result.code}, out: ${result.out}, err: ${result.err}")
    return result.out.joinToString("\n")
}

fun setSepolicy(pkg: String, rules: String): Boolean {
    val shell = getRootShell()
    val result = shell.newJob().add("${getKsuDaemonPath()} profile set-sepolicy $pkg '$rules'")
        .to(ArrayList(), null).exec()
    Log.i(TAG, "set sepolicy result: ${result.code}")
    return result.isSuccess
}

fun listAppProfileTemplates(): List<String> {
    val shell = getRootShell()
    return shell.newJob().add("${getKsuDaemonPath()} profile list-templates").to(ArrayList(), null)
        .exec().out
}

fun getAppProfileTemplate(id: String): String {
    val shell = getRootShell()
    return shell.newJob().add("${getKsuDaemonPath()} profile get-template '${id}'")
        .to(ArrayList(), null).exec().out.joinToString("\n")
}

fun setAppProfileTemplate(id: String, template: String): Boolean {
    val shell = getRootShell()
    val escapedTemplate = template.replace("'", "'\\''")
    val cmd = """${getKsuDaemonPath()} profile set-template "$id" '$escapedTemplate'"""
    return shell.newJob().add(cmd)
        .to(ArrayList(), null).exec().isSuccess
}

fun deleteAppProfileTemplate(id: String): Boolean {
    val shell = getRootShell()
    return shell.newJob().add("${getKsuDaemonPath()} profile delete-template '${id}'")
        .to(ArrayList(), null).exec().isSuccess
}

fun forceStopApp(packageName: String, userId: Int? = null) {
    val shell = getRootShell()
    val userArg = userId?.let { " --user $it" } ?: ""
    val result = shell.newJob().add("am force-stop$userArg $packageName").exec()
    Log.i(TAG, "force stop $packageName result: $result")
}

fun launchApp(packageName: String, userId: Int? = null) {
    val shell = getRootShell()
    val userArg = userId?.let { " --user $it" } ?: ""
    val result =
        shell.newJob()
            .add("cmd package resolve-activity --brief$userArg $packageName | tail -n 1 | xargs cmd activity start-activity$userArg -n")
            .exec()
    Log.i(TAG, "launch $packageName result: $result")
}

fun restartApp(packageName: String, userId: Int? = null) {
    forceStopApp(packageName, userId)
    launchApp(packageName, userId)
}
