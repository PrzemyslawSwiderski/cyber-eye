package com.pswidersk.cybereyeapp.ui.components

import android.util.Log
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.pswidersk.cybereyeapp.CameraClient
import com.pswidersk.cybereyeapp.TAG
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Colors.background
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.labelSize
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.titleSize
import kotlinx.coroutines.launch
import kotlin.math.abs

private const val DEFAULT_EXPOSURE = 80f
private const val DEFAULT_QUALITY = 45f

@Composable
fun ControlPanel() {
    var exposure by remember { mutableFloatStateOf(DEFAULT_EXPOSURE) }
    var quality by remember { mutableFloatStateOf(DEFAULT_QUALITY) }
    val coroutineScope = rememberCoroutineScope()
    var lastSentQuality = DEFAULT_QUALITY

    fun sendQualityCommand(value: Float) {
        if (abs(value - lastSentQuality) < 1f) return
        lastSentQuality = value
        val level = value.toInt()
        Log.d(TAG, "Sending quality command: quality:$level")
        coroutineScope.launch { CameraClient.sendCommand("quality:$level") }
    }

    fun sendExposureCommand(value: Float) {
        val level = value.toInt()
        Log.d(TAG, "Sending exposure command: exposure:$level")
        coroutineScope.launch { CameraClient.sendCommand("exposure:$level") }
    }

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
            label = "Exposure",
            value = exposure,
            onValueChange = { exposure = it; sendExposureCommand(it) },
            valueRange = 2f..235f
        )

        ControlSlider(
            icon = "📊",
            label = "Quality",
            value = quality,
            onValueChange = { quality = it; sendQualityCommand(it) },
            valueRange = 0f..51f
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
        targetState = quality.toInt(),
        transitionSpec = {
            fadeIn() + slideInHorizontally() togetherWith
                    fadeOut() + slideOutHorizontally()
        }
    ) { level ->
        Text(
            text = when (level) {
                in 0..10 -> "Best image quality"
                in 11..20 -> "High quality"
                in 21..30 -> "Balanced"
                in 31..40 -> "Low quality"
                else -> "Lowest quality — best performance"
            },
            color = Color.Cyan,
            fontSize = labelSize,
        )
    }
}