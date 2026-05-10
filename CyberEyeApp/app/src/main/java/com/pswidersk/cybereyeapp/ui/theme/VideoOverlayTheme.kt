package com.pswidersk.cybereyeapp.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.sp

object VideoOverlayTheme {

    object Fonts {
        val labelSize: TextUnit = 11.sp   // dim labels (FPS, Loss…)
        val valueSize: TextUnit = 13.sp   // monospace readouts
        val titleSize: TextUnit = 13.sp   // panel headers
        val metaSize: TextUnit = 10.sp   // secondary info (IP, quality hint)

    }

    object Colors {
        val background: Color = Color.Black.copy(alpha = 0.55f)
    }

}