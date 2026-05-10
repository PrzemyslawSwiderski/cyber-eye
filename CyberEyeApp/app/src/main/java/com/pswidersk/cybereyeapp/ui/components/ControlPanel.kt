package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Colors.background
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.labelSize
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.titleSize

@Composable
fun ControlPanel(
    brightness: Float,
    quality: Float,
    onBrightnessChange: (Float) -> Unit,
    onQualityChange: (Float) -> Unit
) {
    Column(
        modifier = Modifier
            .width(700.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(background)
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp)
    ) {
        OverlayHeader()

        ControlSlider(
            icon = "☀️",
            label = "Brightness",
            value = brightness,
            onValueChange = onBrightnessChange,
            valueRange = 0.1f..1.0f
        )

        ControlSlider(
            icon = "📊",
            label = "Quality",
            value = quality,
            onValueChange = onQualityChange,
            valueRange = 0.0f..1.0f
        )
        QualityLevelIndicator(quality = quality)

    }
}

@Composable
fun OverlayHeader() {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = "Controls",
            color = Color.White,
            fontSize = titleSize,
        )
    }
}

@Composable
fun QualityLevelIndicator(quality: Float) {
    AnimatedContent(
        targetState = (quality * 100).toInt(),
        transitionSpec = {
            fadeIn() + slideInHorizontally() togetherWith
                    fadeOut() + slideOutHorizontally()
        }
    ) { qualityPercent ->
        Text(
            text = when (qualityPercent) {
                in 0..20 -> "Low — best performance"
                in 21..40 -> "Medium-low"
                in 41..60 -> "Medium — balanced"
                in 61..80 -> "Medium-high"
                else -> "High — best image"
            },
            color = Color.Cyan,
            fontSize = labelSize,
        )
    }
}