package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.CameraClient
import kotlinx.coroutines.launch

@Composable
fun WifiSection() {
    val focusManager = LocalFocusManager.current
    val coroutineScope = rememberCoroutineScope()
    var ssidInput by remember { mutableStateOf("") }
    var passwordInput by remember { mutableStateOf("") }

    Column {
        SectionTitle("Wifi")

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedTextField(
                value = ssidInput,
                onValueChange = { ssidInput = it },
                label = { Text("SSID", fontSize = 11.sp) },
                singleLine = true,
                textStyle = TextStyle(fontSize = 13.sp),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                modifier = Modifier.weight(1f)
            )
            OutlinedTextField(
                value = passwordInput,
                onValueChange = { passwordInput = it },
                label = { Text("Password", fontSize = 11.sp) },
                singleLine = true,
                textStyle = TextStyle(fontSize = 13.sp),
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() }),
                modifier = Modifier.weight(1f)
            )
            FilledTonalButton(
                onClick = {
                    focusManager.clearFocus()
                    val command = "wifi_sta:::$ssidInput:::$passwordInput"
                    coroutineScope.launch { CameraClient.sendCommand(command) }
                },
                enabled = ssidInput.isNotBlank()
            ) {
                Text("STA", fontSize = 11.sp)
            }
        }

        Spacer(Modifier.height(16.dp))

        FilledTonalButton(
            onClick = {
                focusManager.clearFocus()
                coroutineScope.launch { CameraClient.sendCommand("wifi_ap") }
            },
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("Switch to Access Point mode", fontSize = 11.sp)
        }
    }

}