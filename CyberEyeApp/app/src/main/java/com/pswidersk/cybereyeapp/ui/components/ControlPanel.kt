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
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
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
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.CameraClient
import com.pswidersk.cybereyeapp.TAG
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Colors.background
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.labelSize
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.titleSize
import kotlinx.coroutines.launch

private const val DEFAULT_EXPOSURE = 80f
private const val DEFAULT_QUALITY = 45f
private const val DEFAULT_BITRATE = 5000f

@Composable
fun ControlPanel() {
    var exposure by remember { mutableFloatStateOf(DEFAULT_EXPOSURE) }
    var quality by remember { mutableFloatStateOf(DEFAULT_QUALITY) }
    var bitrate by remember { mutableFloatStateOf(DEFAULT_BITRATE) }
    val coroutineScope = rememberCoroutineScope()

    fun sendUpdateCommand() {
        val qualityLevel = quality.toInt()
        val exposureLevel = exposure.toInt()
        val bitrateLevel = bitrate.toInt()
        val command = "camera:::qual:$qualityLevel:::exp:$exposureLevel:::bit:$bitrateLevel"
        Log.d(TAG, "Sending update command: $command")
        coroutineScope.launch {
            CameraClient.sendCommand(command)
            AppState.requestVideoReload()
        }
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
            onValueChange = { exposure = it },
            valueRange = 2f..235f
        )

        ControlSlider(
            icon = "📊",
            label = "Quality",
            value = quality,
            onValueChange = { quality = it },
            valueRange = 0f..51f
        )
        QualityLevelIndicator(quality = quality)

        ControlSlider(
            icon = "📡",
            label = "Bitrate (kbps)",
            value = bitrate,
            onValueChange = { bitrate = it },
            valueRange = 500f..20000f
        )
        BitrateLevelIndicator(bitrate = bitrate)

        Spacer(modifier = Modifier.height(8.dp))

        Button(
            onClick = { sendUpdateCommand() },
            modifier = Modifier
                .fillMaxWidth()
                .height(50.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = Color(0xFF2196F3),
                contentColor = Color.White
            ),
            shape = RoundedCornerShape(8.dp)
        ) {
            Text(
                text = "Apply Settings",
                fontSize = labelSize
            )
        }
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

@Composable
fun BitrateLevelIndicator(bitrate: Float) {
    AnimatedContent(
        targetState = bitrate.toInt(),
        transitionSpec = {
            fadeIn() + slideInHorizontally() togetherWith
                    fadeOut() + slideOutHorizontally()
        }
    ) { level ->
        Text(
            text = when (level) {
                in 0..1000 -> "Very low bandwidth"
                in 1001..3000 -> "Low bandwidth"
                in 3001..6000 -> "Standard quality"
                in 6001..10000 -> "HD quality"
                in 10001..15000 -> "Full HD quality"
                else -> "Ultra HD quality"
            },
            color = Color.Cyan,
            fontSize = labelSize,
        )
    }
}