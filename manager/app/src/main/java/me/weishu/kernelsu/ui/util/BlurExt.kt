package me.weishu.kernelsu.ui.util

import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import top.yukonga.miuix.kmp.blur.LayerBackdrop

/**
 * 顶栏/底栏模糊已移除。
 *
 * 原实现用 textureBlur + surface(alpha 0.87) 混合绘制顶栏/底栏，
 * 在自定义窗口背景场景下会导致顶栏与底栏区域整体发黑，
 * 因此这里始终返回 null，各页面顶栏/底栏回退为不透明 surface 颜色。
 */
@Composable
@Suppress("UNUSED_PARAMETER")
fun rememberBlurBackdrop(enableBlur: Boolean): LayerBackdrop? = null

/**
 * 保留调用签名以便各页面顶栏/底栏统一包裹，但不再做任何模糊绘制。
 */
@Composable
@Suppress("UNUSED_PARAMETER")
fun BlurredBar(
    backdrop: LayerBackdrop?,
    blurActive: Boolean = true,
    content: @Composable () -> Unit,
) {
    Box(modifier = Modifier) {
        content()
    }
}
