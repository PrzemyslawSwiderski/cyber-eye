package com.pswidersk.cybereyeapp

import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.pswidersk.cybereyeapp.AppState.cameraIp
import com.pswidersk.cybereyeapp.h264.RtpReceiver
import com.pswidersk.cybereyeapp.model.StatusResponse
import com.pswidersk.cybereyeapp.model.Status
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.DeserializationStrategy
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress

private val json = Json { ignoreUnknownKeys = true }

class VideoViewModel : ViewModel() {

    private lateinit var socket: DatagramSocket

    lateinit var rtpReceiver: RtpReceiver
        private set

    private var stopJob: Job? = null

    fun initCommunication() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (!::socket.isInitialized || socket.isClosed) {
                    socket = DatagramSocket(null).apply {
                        reuseAddress = true
                        soTimeout = 2000
                    }
                }
                if (!::rtpReceiver.isInitialized) {
                    rtpReceiver = RtpReceiver(socket)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error: ${e.message}")
            }
        }
    }

    suspend fun sendCommand(
        command: String
    ): Boolean = withContext(Dispatchers.IO) {
        val response = requestCommand(command, StatusResponse.serializer())
        return@withContext !(response == null || response.status != Status.OK)
    }

    suspend fun <T> requestCommand(
        command: String,
        deserializer: DeserializationStrategy<T>
    ): T? = withContext(Dispatchers.IO) {
        val addr = InetSocketAddress(cameraIp.value, CONTROL_PORT)
        val data = command.toByteArray()

        try {
            socket.send(DatagramPacket(data, data.size, addr))

            val buf = ByteArray(4096)
            val reply = DatagramPacket(buf, buf.size)
            socket.receive(reply)

            val raw = String(reply.data, 0, reply.length, Charsets.UTF_8)
            Log.d(TAG, "Command '$command' → $raw")
            json.decodeFromString(deserializer, raw)
        } catch (ex: Exception) {
            Log.e(TAG, "Error response for '$command'", ex)
            null
        }
    }

    fun scheduleStop() {
        stopJob = viewModelScope.launch(Dispatchers.IO) {
            val success = sendCommand("stop")
            if (!success) {
                Log.e(TAG, "Stop failed")
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        runBlocking { stopJob?.join() }
        if (::socket.isInitialized) socket.close()
        if (::rtpReceiver.isInitialized) rtpReceiver.stop()
        Log.d(TAG, "Socket closed")
    }
}