package com.pswidersk.cybereyeapp.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight.Companion.Bold
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.ACCESS_POINT_IP
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.ui.components.RebootCamera

@Composable
fun HomeScreen(
    modifier: Modifier = Modifier,
    onShowVideoClick: () -> Unit,
    onRebootClick: () -> Unit,
    onCheckStatusClick: () -> Unit,
) {
    var ipInput by remember { AppState.cameraIp }
    var cameraStatus by remember { AppState.cameraStatus }
    val focusManager = LocalFocusManager.current
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

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = ipInput,
                        onValueChange = { ipInput = it },
                        label = { Text("Camera IP", fontSize = 11.sp) },
                        singleLine = true,
                        textStyle = TextStyle(fontSize = 13.sp, fontWeight = Bold),
                        keyboardOptions = KeyboardOptions(
                            keyboardType = KeyboardType.Uri,
                            imeAction = ImeAction.Done
                        ),
                        keyboardActions = KeyboardActions(
                            onDone = {
                                AppState.cameraIp.value = ipInput
                                focusManager.clearFocus()
                            }
                        ),
                        modifier = Modifier.weight(1f)
                    )
                    OutlinedButton(
                        onClick = {
                            ipInput = ACCESS_POINT_IP
                            AppState.cameraIp.value = ACCESS_POINT_IP
                            focusManager.clearFocus()
                        }
                    ) {
                        Text("Access Point IP", fontSize = 11.sp)
                    }
                }

                Spacer(Modifier.height(16.dp))

                Button(
                    onClick = {
                        AppState.cameraIp.value = ipInput
                        onShowVideoClick()
                    },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Show Video Stream")
                }

                Spacer(Modifier.height(4.dp))
                Text(
                    "Camera must be on the same network",
                    fontSize = 11.sp,
                    color = Color.Gray
                )

                Spacer(Modifier.height(16.dp))
                HorizontalDivider()
                Spacer(Modifier.height(16.dp))

                // Status row
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    OutlinedButton(
                        onClick = onCheckStatusClick,
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
                        text = cameraStatus,
                        fontSize = 14.sp,
                        fontWeight = Bold,
                        color = when (cameraStatus) {
                            "OK", "READY" -> Color(0xFF4CCB4E)
                            "STREAMING" -> Color(0xFF1B65D9)
                            else -> Color(0xFFC62828)
                        },
                    )
                }

                Spacer(Modifier.height(16.dp))
                HorizontalDivider()
                Spacer(Modifier.height(16.dp))

                RebootCamera(onRebootClick)
            }
        }
    }
}