package com.pswidersk.cybereyeapp.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

const val ESP32_IP = "192.168.1.17"
const val CONTROL_PORT = 3334

@Composable
fun HomeScreen(
    modifier: Modifier = Modifier,
    onShowVideoClick: () -> Unit
) {
    Column(
        modifier = modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Card(
            modifier = Modifier
                .padding(16.dp)
                .width(350.dp),
            elevation = CardDefaults.cardElevation(4.dp)
        ) {
            Column(
                modifier = Modifier
                    .padding(24.dp)
                    .fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    "CyberEye Settings",
                    fontSize = 24.sp,
                    style = MaterialTheme.typography.titleLarge
                )
                Spacer(Modifier.height(16.dp))

                Text(
                    "Stream Configuration",
                    fontSize = 18.sp,
                    style = MaterialTheme.typography.titleMedium
                )
                Spacer(Modifier.height(8.dp))

                Text(
                    "ESP32 IP: $ESP32_IP",
                    fontSize = 14.sp,
                    color = Color.Gray
                )
                Text(
                    "Control Port: $CONTROL_PORT",
                    fontSize = 14.sp,
                    color = Color.Gray
                )

                Spacer(Modifier.height(24.dp))

                Button(
                    onClick = onShowVideoClick,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Show Video Stream")
                }

                Spacer(Modifier.height(8.dp))

                Text(
                    "Note: ESP32 must be connected to the same network",
                    fontSize = 11.sp,
                    color = Color.Gray
                )
            }
        }
    }
}