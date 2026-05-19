package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight.Companion.Bold
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.CameraClient
import com.pswidersk.cybereyeapp.model.Status
import com.pswidersk.cybereyeapp.model.StatusResponse
import kotlinx.coroutines.launch

@Composable
fun CameraStatusSection() {
    var cameraStatus by remember { AppState.cameraStatus }
    val coroutineScope = rememberCoroutineScope()

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        OutlinedButton(
            onClick = {
                coroutineScope.launch {
                    val response = CameraClient.requestCommand(
                        "status",
                        StatusResponse.serializer(),
                        StatusResponse()
                    )
                    AppState.cameraStatus.value = response.status
                }
            },
            modifier = Modifier.wrapContentWidth()
        ) {
            Icon(
                imageVector = Icons.Default.Info,
                contentDescription = "Check status",
                modifier = Modifier.size(16.dp)
            )
            Spacer(Modifier.width(20.dp))
            Text("Check Status", fontSize = 13.sp)
        }
        Spacer(Modifier.width(15.dp))
        Text(
            text = cameraStatus.name,
            fontSize = 14.sp,
            fontWeight = Bold,
            color = when (cameraStatus) {
                Status.OK, Status.READY -> Color(0xFF4CCB4E)
                Status.STREAMING -> Color(0xFF1B65D9)
                Status.PENDING -> Color(0xFFA88A12)
                else -> Color(0xFFC62828)
            },
        )
    }
}