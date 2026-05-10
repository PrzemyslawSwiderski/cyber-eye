package com.pswidersk.cybereyeapp.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.pswidersk.cybereyeapp.ui.components.ControlPanel
import com.pswidersk.cybereyeapp.ui.components.StatsPanel

@Composable
fun VideoScreen(
    visible: Boolean,
    brightness: Float,
    quality: Float,
    onBrightnessChange: (Float) -> Unit,
    onQualityChange: (Float) -> Unit,
) {
    var showControlPanel by remember { mutableStateOf(false) }
    var showStatsPanel by remember { mutableStateOf(false) }

    AnimatedVisibility(
        visible = visible,
        enter = slideInVertically(
            initialOffsetY = { it },
            animationSpec = tween(300)
        ) + fadeIn(),
        exit = slideOutVertically(
            targetOffsetY = { it },
            animationSpec = tween(300)
        ) + fadeOut()
    ) {
        Box(modifier = Modifier.fillMaxSize()) {

            Row(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(16.dp),
                verticalAlignment = Alignment.Top
            ) {
                AnimatedVisibility(
                    visible = showControlPanel,
                    enter = fadeIn(animationSpec = tween(200)),
                    exit = fadeOut(animationSpec = tween(200))
                ) {
                    ControlPanel(
                        brightness = brightness,
                        quality = quality,
                        onBrightnessChange = onBrightnessChange,
                        onQualityChange = onQualityChange
                    )
                }

                IconButton(onClick = { showControlPanel = !showControlPanel }) {
                    Icon(
                        imageVector = Icons.Default.Settings,
                        contentDescription = "Settings",
                        tint = Color.Red
                    )
                }
            }

            Row(
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .padding(16.dp),
                verticalAlignment = Alignment.Bottom
            ) {
                AnimatedVisibility(
                    visible = showStatsPanel,
                    enter = fadeIn(animationSpec = tween(200)),
                    exit = fadeOut(animationSpec = tween(200))
                ) {
                    StatsPanel()
                }
                IconButton(onClick = { showStatsPanel = !showStatsPanel }) {
                    Icon(
                        imageVector = Icons.Default.Info,
                        contentDescription = "Info",
                        tint = Color.Red
                    )
                }
            }
        }
    }
}