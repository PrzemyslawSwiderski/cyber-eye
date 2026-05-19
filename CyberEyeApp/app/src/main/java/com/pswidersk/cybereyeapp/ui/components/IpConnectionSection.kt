package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight.Companion.Bold
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.ACCESS_POINT_IP
import com.pswidersk.cybereyeapp.AppState

@Composable
fun IpConnectionSection() {
    val focusManager = LocalFocusManager.current
    var ipInput by remember { AppState.cameraIp }

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
}