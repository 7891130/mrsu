plugins {
    alias(libs.plugins.agp.app) apply false
    alias(libs.plugins.kotlin) apply false
    alias(libs.plugins.compose.compiler) apply false
}

extra["androidMinSdkVersion"] = 31
extra["androidTargetSdkVersion"] = 37
extra["androidCompileSdkVersion"] = 37
extra["androidCompileSdkVersionMinor"] = 0
extra["androidBuildToolsVersion"] = "37.0.0"
extra["androidCompileNdkVersion"] = libs.versions.ndk.get()
extra["androidSourceCompatibility"] = JavaVersion.VERSION_21
extra["androidTargetCompatibility"] = JavaVersion.VERSION_21
extra["managerVersionCode"] = getVersionCode()
extra["managerVersionName"] = getVersionName()

fun getGitCommitCount(): Int {
    val process = Runtime.getRuntime().exec(arrayOf("git", "rev-list", "--count", "HEAD"))
    return process.inputStream.bufferedReader().use { it.readText().trim().toInt() }
}

fun getGitDescribe(): String {
    val process = Runtime.getRuntime().exec(arrayOf("git", "describe", "--tags", "--always"))
    return process.inputStream.bufferedReader().use { it.readText().trim() }
}

// MRSU: 版本号基线抬高，避免与上游 30000+ 段位混淆；
// 可用 -PmrsuVersionCode=xxxxx 显式指定，发版时与自有 latest.json 保持一致。
fun getVersionCode(): Int {
    val override = providers.gradleProperty("mrsuVersionCode").orNull
    if (!override.isNullOrBlank()) {
        return override.trim().toInt()
    }
    val commitCount = runCatching { getGitCommitCount() }.getOrDefault(1)
    return 33000 + commitCount
}

fun getVersionName(): String {
    return getGitDescribe()
}
