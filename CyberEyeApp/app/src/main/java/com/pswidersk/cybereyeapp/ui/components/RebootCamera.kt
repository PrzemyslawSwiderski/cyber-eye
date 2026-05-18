package com.pswidersk.cybereyeapp.ui.components

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

@Composable
fun RebootCamera(
    onConfirm: () -> Unit
) {
    var showDialog by remember { mutableStateOf(false) }

    if (showDialog) {
        AlertDialog(
            onDismissRequest = { showDialog = false },
            title = { Text("Reboot Camera?") },
            text = { Text("Are you sure you want to reboot the camera?") },
            confirmButton = {
                Button(
                    onClick = {
                        showDialog = false
                        onConfirm()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = Color.Red)
                ) {
                    Text("Reboot")
                }
            },
            dismissButton = {
                OutlinedButton(onClick = { showDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }

    Button(
        onClick = { showDialog = true },
        modifier = Modifier.width(200.dp),
        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFC62828))
    ) {
        Icon(
            imageVector = Icons.Default.Refresh,
            contentDescription = "Reboot",
            modifier = Modifier.size(16.dp)
        )
        Spacer(Modifier.width(6.dp))
        Text("Reboot Camera")
    }
}