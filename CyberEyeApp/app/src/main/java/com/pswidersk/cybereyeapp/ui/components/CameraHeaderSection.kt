package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.ElevatedButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight.Companion.Bold
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun CameraHeaderSection(onShowVideoClick: () -> Unit) {
    Text(
        "Cyber Eye", style = MaterialTheme.typography.titleLarge,
        fontWeight = Bold,
    )

    Spacer(Modifier.height(16.dp))

    ElevatedButton(
        onClick = {
            onShowVideoClick()
        },
        modifier = Modifier.fillMaxWidth()
    ) {
        Text("Show Video Stream")
    }

    Spacer(Modifier.height(7.dp))

    Text(
        "Camera must be on the same network",
        fontSize = 11.sp
    )

    Spacer(Modifier.height(7.dp))

    CameraStatusSection()
}