package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight.Companion.Bold
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.CameraClient
import com.pswidersk.cybereyeapp.model.CameraStatus
import kotlinx.coroutines.launch

@Composable
fun CameraStatusSection() {
    val cameraInfo by AppState.cameraInfo.collectAsState()
    val coroutineScope = rememberCoroutineScope()

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("Camera Status:", fontSize = 13.sp)
        Spacer(Modifier.width(15.dp))
        Text(
            text = cameraInfo.status.name,
            fontSize = 14.sp,
            fontWeight = Bold,
            color = when (cameraInfo.status) {
                CameraStatus.OK, CameraStatus.READY -> Color(0xFF4CCB4E)
                CameraStatus.STREAMING -> Color(0xFF1B65D9)
                CameraStatus.PENDING -> Color(0xFFA88A12)
                else -> Color(0xFFC62828)
            },
        )
    }

    if (cameraInfo.lastError.isNotBlank()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("Last Error:", fontSize = 13.sp)
            Spacer(Modifier.width(15.dp))
            Text(
                text = cameraInfo.lastError,
                fontSize = 12.sp,
                color = Color(0xFFC62828),
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedButton(
                onClick = {
                    coroutineScope.launch {
                        CameraClient.sendCommand("clear_error")
                    }
                },
                colors = ButtonDefaults.outlinedButtonColors(containerColor = Color.Yellow)
            ) {
                Text("Clear")
            }
        }
    }
}
