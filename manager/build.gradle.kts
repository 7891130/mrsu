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
    // MRSU: 发版时手动递增（1.0.0 -> 33015，1.0.1 -> 33016，1.0.2 -> 33017，1.0.3 -> 33018，1.0.4 -> 33019，1.0.5 -> 33020，1.0.6 -> 33021，1.0.7 -> 33022，1.0.8 -> 33023，1.0.9 -> 33024，1.0.9.1 -> 33025，1.0.9.2 -> 33026，1.0.9.3 -> 33032，1.0.9.4 -> 33033，1.0.9.5 -> 33034，1.0.9.6 -> 33035，1.0.9.7 -> 33036，1.0.9.8 -> 33037，1.0.9.9 -> 33038，1.0.9.9.1 -> 33039，1.1-beta -> 33040）
    return 33040
}

// MRSU: 版本号改为语义化版本，不再跟随 git describe；
// 可用 -PmrsuVersionName=x.y.z 显式指定。
fun getVersionName(): String {
    val override = providers.gradleProperty("mrsuVersionName").orNull
    if (!override.isNullOrBlank()) {
        return override.trim()
    }
    return "1.1-beta"
}
