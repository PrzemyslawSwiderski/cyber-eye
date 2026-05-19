package com.pswidersk.cybereyeapp.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight.Companion.Bold
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.ui.components.CameraRebootSection
import com.pswidersk.cybereyeapp.ui.components.CameraStatusSection
import com.pswidersk.cybereyeapp.ui.components.IpConnectionSection
import com.pswidersk.cybereyeapp.ui.components.WifiSection

@Composable
fun HomeScreen(
    modifier: Modifier = Modifier,
    onShowVideoClick: () -> Unit
) {
    val scrollState = rememberScrollState()

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(scrollState),
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
                    "Cyber Eye", style = MaterialTheme.typography.titleLarge,
                    fontWeight = Bold,
                    color = Color(0xFF21A821)
                )
                Spacer(Modifier.height(16.dp))

                IpConnectionSection()

                Spacer(Modifier.height(16.dp))

                Button(
                    onClick = {
                        onShowVideoClick()
                    },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Show Video Stream")
                }

                Spacer(Modifier.height(16.dp))

                Text(
                    "Camera must be on the same network",
                    fontSize = 11.sp,
                    color = Color.Gray
                )
                
                Spacer(Modifier.height(16.dp))

                WifiSection()

                Spacer(Modifier.height(16.dp))
                HorizontalDivider()
                Spacer(Modifier.height(16.dp))

                CameraStatusSection()

                Spacer(Modifier.height(16.dp))
                HorizontalDivider()
                Spacer(Modifier.height(16.dp))

                CameraRebootSection()
            }
        }
    }
}