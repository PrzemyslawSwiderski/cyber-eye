package com.pswidersk.cybereyeapp

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
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
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.ui.theme.CyberEyeAppTheme
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import java.net.SocketTimeoutException

class MainActivity : ComponentActivity() {

    companion object {
        private const val TAG = "UDPReceiver"
        const val ESP32_IP = "192.168.1.17"
        const val DATA_PORT = 3333
        const val CONTROL_PORT = 3334
        const val APP_SOCKET_PORT = 3337
    }

    private var receivedTimestamp by mutableStateOf("Waiting...")
    private var isReceiving by mutableStateOf(false)
    private var receivingJob: Job? = null

    private val socket = DatagramSocket(APP_SOCKET_PORT).apply {
        reuseAddress = true
        soTimeout = 2000
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            CyberEyeAppTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    TimestampDisplay(
                        timestamp = receivedTimestamp,
                        isReceiving = isReceiving,
                        onStartClick = { startReceiving() },
                        onStopClick = { stopReceiving() },
                        modifier = Modifier.padding(innerPadding)
                    )
                }
            }
        }
    }

    private fun startReceiving() {
        if (!isReceiving) {
            receivingJob = CoroutineScope(Dispatchers.IO).launch {
                startUdpReceiving()
            }
        }
    }

    private fun stopReceiving() {
        CoroutineScope(Dispatchers.IO).launch {
            isReceiving = false
            receivedTimestamp = "Stopped"
            sendUdpCommand("stop", CONTROL_PORT)
        }
    }

    private suspend fun sendUdpCommand(command: String, port: Int) = withContext(Dispatchers.IO) {
        try {
            val address = InetSocketAddress(ESP32_IP, port)
            socket.connect(address)
            val data = command.toByteArray()
            val packet = DatagramPacket(data, data.size)
            socket.send(packet)

            // Wait for acknowledgment
            val buffer = ByteArray(32)
            val receivePacket = DatagramPacket(buffer, buffer.size)
            try {
                socket.receive(receivePacket)
                val response = String(receivePacket.data, 0, receivePacket.length)
                Log.d(TAG, "Command '$command' response: $response")
            } catch (_: Exception) {
                Log.w(TAG, "No response for command: $command")
            }
            Log.d(TAG, "Sent UDP command: $command to port $port")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to send UDP command: ${e.message}")
        }
    }

    private suspend fun startUdpReceiving() = withContext(Dispatchers.IO) {
        var lastUpdateTime = 0L
        var latestTimestamp: String

        try {
            // Send start command via control port
            sendUdpCommand("start", CONTROL_PORT)
            Log.d(TAG, "Sent start command via control port $CONTROL_PORT")

            // Create socket for receiving data
            val address = InetSocketAddress(ESP32_IP, DATA_PORT)
            socket.connect(address)

            val buffer = ByteArray(128)
            val packet = DatagramPacket(buffer, buffer.size)

            withContext(Dispatchers.Main) {
                isReceiving = true
                receivedTimestamp = "Listening for UDP on port ${socket.localPort}..."
            }

            Log.d(TAG, "UDP data socket created, waiting for timestamps...")

            while (isReceiving) {
                try {
                    socket.receive(packet)
                    val data = String(packet.data, 0, packet.length)

                    // Store the latest timestamp
                    latestTimestamp = data

                    // Update UI only every second
                    val currentTime = System.currentTimeMillis()
                    if (currentTime - lastUpdateTime >= 1000) {
                        withContext(Dispatchers.Main) {
                            receivedTimestamp = latestTimestamp
                        }
                        lastUpdateTime = currentTime
                    }
                } catch (_: SocketTimeoutException) {
                    Log.e(TAG, "Receive timeout")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "UDP error: ${e.message}")
            withContext(Dispatchers.Main) {
                receivedTimestamp = "Error: ${e.message}"
            }
        } finally {
            withContext(Dispatchers.Main) {
                isReceiving = false
            }
        }
    }
}

@Composable
fun TimestampDisplay(
    timestamp: String,
    isReceiving: Boolean,
    onStartClick: () -> Unit,
    onStopClick: () -> Unit,
    modifier: Modifier = Modifier
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
            elevation = CardDefaults.cardElevation(defaultElevation = 4.dp)
        ) {
            Column(
                modifier = Modifier
                    .padding(24.dp)
                    .fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "ESP32 Timestamp",
                    fontSize = 20.sp,
                    style = MaterialTheme.typography.titleLarge
                )

                Spacer(modifier = Modifier.height(16.dp))

                Text(
                    text = timestamp,
                    fontSize = 20.sp,
                    style = MaterialTheme.typography.bodyLarge,
                    color = if (isReceiving) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
                )

                Spacer(modifier = Modifier.height(16.dp))

                Text(
                    text = if (isReceiving) "🟢 Receiving..." else "🔴 Disconnected",
                    fontSize = 14.sp,
                    color = if (isReceiving) Color.Green else Color.Red
                )

                Spacer(modifier = Modifier.height(12.dp))

                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = onStartClick
                    ) {
                        Text("Start")
                    }

                    Button(
                        onClick = onStopClick
                    ) {
                        Text("Stop")
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = "ESP32: ${MainActivity.ESP32_IP} | Data:${MainActivity.DATA_PORT} Ctrl:${MainActivity.CONTROL_PORT}",
                    fontSize = 12.sp,
                    color = Color.Gray
                )
            }
        }
    }
}
