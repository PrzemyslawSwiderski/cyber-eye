package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight.Companion.Bold
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.CameraClient
import kotlinx.coroutines.launch

@Composable
fun MusicControlSection() {
    val coroutineScope = rememberCoroutineScope()
    var volume by remember { mutableFloatStateOf(50f) }
    var isPlaying by remember { mutableStateOf(false) }
    var recordName by remember { mutableStateOf("test.mp3") }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                imageVector = Icons.Default.Favorite,
                contentDescription = "Music",
                modifier = Modifier.height(20.dp),
                tint = Color(0xFF1B65D9)
            )
            Text(
                text = "Music Control",
                fontSize = 14.sp,
                fontWeight = Bold
            )
        }

        OutlinedTextField(
            value = recordName,
            onValueChange = { recordName = it },
            label = { Text("Record Name", fontSize = 12.sp) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            textStyle = androidx.compose.ui.text.TextStyle(fontSize = 14.sp)
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedButton(
                onClick = {
                    coroutineScope.launch {
                        CameraClient.sendCommand("music_play:::file://sdcard/$recordName")
                        isPlaying = true
                    }
                },
                enabled = !isPlaying,
                colors = ButtonDefaults.outlinedButtonColors(
                    containerColor = Color(0xFF4CCB4E)
                )
            ) {
                Icon(
                    imageVector = Icons.Default.PlayArrow,
                    contentDescription = "Play",
                    modifier = Modifier.height(16.dp)
                )
                Spacer(Modifier.width(4.dp))
                Text("Play")
            }

            OutlinedButton(
                onClick = {
                    coroutineScope.launch {
                        CameraClient.sendCommand("music_stop")
                        isPlaying = false
                    }
                },
                enabled = isPlaying,
                colors = ButtonDefaults.outlinedButtonColors(
                    containerColor = Color(0xFFC62828)
                )
            ) {
                Icon(
                    imageVector = Icons.Default.Close,
                    contentDescription = "Stop",
                    modifier = Modifier.height(16.dp)
                )
                Spacer(Modifier.width(4.dp))
                Text("Stop")
            }
        }

        // Volume control
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = "Vol: ${volume.toInt()}%",
                fontSize = 12.sp,
                modifier = Modifier.width(50.dp)
            )

            Slider(
                value = volume,
                onValueChange = { volume = it },
                onValueChangeFinished = {
                    coroutineScope.launch {
                        CameraClient.sendCommand("music_volume:::${volume.toInt()}")
                    }
                },
                valueRange = 0f..100f,
                modifier = Modifier.weight(1f)
            )
        }

        // Status text
        Text(
            text = if (isPlaying) "▶ Playing..." else "■ Stopped",
            fontSize = 12.sp,
            color = if (isPlaying) Color(0xFF4CCB4E) else Color.Gray
        )
    }
}