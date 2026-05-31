package com.pswidersk.cybereyeapp.ui.components

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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Colors.background
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.labelSize
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Fonts.valueSize

@Composable
fun StatsPanel() {
    val stats by AppState.rtpStats.collectAsState()
    val cameraInfo by AppState.cameraInfo.collectAsState()
    var rttMs by remember { AppState.cameraLatency }

    Column(
        modifier = Modifier
            .width(200.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(background)
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        // Existing RTP stats
        StatRow("FPS", "%.1f".format(stats.fps), Color.White)
        StatRow("Bitrate", "%.0f kbps".format(stats.bitrateKbps), Color.White)
        StatRow(
            label = "Loss",
            value = "%.1f%%".format(stats.packetLossPct),
            color = when {
                stats.packetLossPct < 1f -> Color(0xFF4CAF50)
                stats.packetLossPct < 5f -> Color(0xFFFF9800)
                else -> Color(0xFFF44336)
            }
        )
        StatRow(
            label = "Jitter",
            value = "%.1f ms".format(stats.jitterMs),
            color = when {
                stats.jitterMs < 10f -> Color(0xFF4CAF50)
                stats.jitterMs < 30f -> Color(0xFFFF9800)
                else -> Color(0xFFF44336)
            }
        )

        // Camera device stats
        StatRow(
            label = "Lag",
            value = "$rttMs ms",
            color = when {
                rttMs < 50 -> Color(0xFF4CAF50)
                rttMs < 150 -> Color(0xFFFF9800)
                else -> Color(0xFFF44336)
            }
        )
        StatRow(
            label = "Temp",
            value = if (cameraInfo.temp.isNaN()) "N/A" else "%.1f°C".format(cameraInfo.temp),
            color = when {
                cameraInfo.temp.isNaN() -> Color.White
                cameraInfo.temp < 60f -> Color(0xFF4CAF50)
                cameraInfo.temp < 75f -> Color(0xFFFF9800)
                else -> Color(0xFFF44336)
            }
        )
        StatRow(
            label = "Signal",
            value = "${cameraInfo.signal} dBm",
            color = when {
                cameraInfo.signal >= -60 -> Color(0xFF4CAF50)
                cameraInfo.signal >= -75 -> Color(0xFFFF9800)
                else -> Color(0xFFF44336)
            }
        )
        StatRow(
            label = "Heap",
            value = "${cameraInfo.freeHeap / 1024} kB",
            color = when {
                cameraInfo.freeHeap > 100_000 -> Color(0xFF4CAF50)
                cameraInfo.freeHeap > 50_000 -> Color(0xFFFF9800)
                else -> Color(0xFFF44336)
            }
        )
        StatRow(
            label = "Block",
            value = "${cameraInfo.freeBlock / 1024} kB",
            color = when {
                cameraInfo.freeBlock > 50_000 -> Color(0xFF4CAF50)
                cameraInfo.freeBlock > 20_000 -> Color(0xFFFF9800)
                else -> Color(0xFFF44336)
            }
        )
    }
}

@Composable
private fun StatRow(label: String, value: String, color: Color) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            fontSize = labelSize,
            color = Color.White.copy(alpha = 0.6f)
        )
        Text(
            text = value,
            fontSize = valueSize,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            color = color
        )
    }
}