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
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.CameraClient
import com.pswidersk.cybereyeapp.TAG
import com.pswidersk.cybereyeapp.ui.theme.CyberGreenLight
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Colors.background
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.labelSize
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.titleSize
import kotlinx.coroutines.launch

@Composable
fun ControlPanel() {
    val coroutineScope = rememberCoroutineScope()
    val settings by AppState.cameraSettings

    fun sendUpdateCommand() {
        val command = settings.toCommand()
        Log.d(TAG, "Sending update command: $command")
        coroutineScope.launch {
            CameraClient.sendCommand(command)
            AppState.requestVideoReload()
        }
    }
    Surface(
        color = background,
        contentColor = Color.White
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
                label = "Exposure",
                value = settings.exposure,
                onValueChange = { AppState.updateExposure(it) },
                valueRange = 2f..235f
            )

            ControlSlider(
                icon = "📊",
                label = "Quality",
                value = settings.quality,
                onValueChange = { AppState.updateQuality(it) },
                valueRange = 0f..51f
            )
            QualityLevelIndicator(quality = settings.quality)

            Spacer(modifier = Modifier.height(8.dp))

            FilledTonalButton(
                onClick = { sendUpdateCommand() },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(50.dp),
                shape = RoundedCornerShape(8.dp)
            ) {
                Text(
                    text = "Apply Settings",
                    fontSize = labelSize
                )
            }
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
            color = CyberGreenLight,
            fontSize = labelSize,
        )
    }
}
