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

    Column(
        modifier = Modifier
            .width(200.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(background)
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
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