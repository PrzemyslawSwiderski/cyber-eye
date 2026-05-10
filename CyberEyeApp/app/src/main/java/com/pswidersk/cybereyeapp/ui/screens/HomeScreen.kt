package com.pswidersk.cybereyeapp.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.ACCESS_POINT_IP
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.DEFAULT_IP


@Composable
fun HomeScreen(
    modifier: Modifier = Modifier,
    onShowVideoClick: () -> Unit
) {
    var ipInput by remember { mutableStateOf(AppState.cameraIp.value.ifEmpty { DEFAULT_IP }) }
    val focusManager = LocalFocusManager.current

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
                    style = MaterialTheme.typography.titleLarge
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
                        textStyle = TextStyle(fontSize = 13.sp),
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

                Spacer(Modifier.height(8.dp))

                Text(
                    "Camera must be on the same network",
                    fontSize = 11.sp,
                    color = Color.Gray
                )
            }
        }
    }
}