package me.weishu.kernelsu.ui.component

import top.yukonga.miuix.kmp.nav.transition.NavMotion
import top.yukonga.miuix.kmp.nav.transition.NavSwipeDirection
import top.yukonga.miuix.kmp.nav.transition.NavTransition
import top.yukonga.miuix.kmp.nav.transition.navGraphicsTransition

/**
 * Cross fade between screens so that the wallpaper stays put.
 *
 * The picture is painted once, on the window layer, by [AppBackground]. The stock transition
 * slides the pages sideways, which makes the wallpaper look like it slides away too. With this
 * transition nothing translates: the entering page just fades its content in on top of the
 * leaving one, and going back fades it out again, so the picture behind never moves.
 *
 * [top.yukonga.miuix.kmp.nav.transition.NavTransitionScope.relativeDepth] is 0 for the page at
 * rest, grows to 1 for the page covering the previous one and drops to -1 for the page being
 * left behind, so the depth can be turned into an alpha directly.
 */
val PageFadeTransition: NavTransition = navGraphicsTransition(
    opaqueDepth = 1f,               // pages below the top two are never composed
    dismissDirection = NavSwipeDirection.LeftToRight, // keep the swipe back gesture working
    motion = NavMotion(),           // stock settle specs
    scrim = { 0f },                 // no extra scrim, the layers already fade each other
) { scope ->
    val depth = scope.relativeDepth
    alpha = when {
        depth > 0f -> 1f - depth.coerceIn(0f, 1f) // covering page fades in
        else -> 1f + depth.coerceIn(-1f, 0f)      // covered page fades out
    }
    translationX = 0f
    translationY = 0f
    scaleX = 1f
    scaleY = 1f
}
