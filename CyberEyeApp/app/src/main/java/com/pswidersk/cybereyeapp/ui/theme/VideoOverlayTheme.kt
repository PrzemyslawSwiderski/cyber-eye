package com.pswidersk.cybereyeapp.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.sp

object VideoOverlayTheme {

    object Fonts {
        val titleSize: TextUnit = 15.sp   // panel headers
        val labelSize: TextUnit = 14.sp   // dim labels (FPS, Loss…)
        val valueSize: TextUnit = 12.sp   // monospace readouts

    }

    object Colors {
        val background: Color = Color.Black.copy(alpha = 0.55f)
    }

}