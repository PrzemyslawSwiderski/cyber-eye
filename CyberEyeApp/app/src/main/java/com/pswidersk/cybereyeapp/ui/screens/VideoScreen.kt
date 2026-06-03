package com.pswidersk.cybereyeapp.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Refresh
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
import androidx.compose.ui.unit.dp
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.ui.components.ControlPanel
import com.pswidersk.cybereyeapp.ui.components.StatsPanel
import com.pswidersk.cybereyeapp.ui.theme.CyberRed

@Composable
fun VideoScreen(onScreenshot: () -> Unit = {}) {
    var showControlPanel by remember { mutableStateOf(false) }
    var showStatsPanel by remember { mutableStateOf(true) }

    Box(modifier = Modifier.fillMaxSize()) {

        Row(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(horizontal = 70.dp, vertical = 16.dp),
            verticalAlignment = Alignment.Top
        ) {
            IconButton(onClick = onScreenshot) {
                Icon(
                    imageVector = Icons.Default.Favorite,
                    contentDescription = "Take screenshot",
                    tint = CyberRed
                )
            }
        }

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
                ControlPanel()
            }

            IconButton(onClick = { showControlPanel = !showControlPanel }) {
                Icon(
                    imageVector = Icons.Default.Settings,
                    contentDescription = "Settings",
                    tint = CyberRed
                )
            }
        }

        Row(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(horizontal = 70.dp, vertical = 16.dp),
            verticalAlignment = Alignment.Bottom
        ) {
            IconButton(
                onClick = { AppState.requestVideoReload() }
            ) {
                Icon(
                    imageVector = Icons.Default.Refresh,
                    contentDescription = "Restart stream",
                    tint = CyberRed
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
                    tint = CyberRed
                )
            }
        }
    }
}