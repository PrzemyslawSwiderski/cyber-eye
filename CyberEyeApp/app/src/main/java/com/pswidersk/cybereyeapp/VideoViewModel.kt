package com.pswidersk.cybereyeapp

import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.pswidersk.cybereyeapp.AppState.cameraIp
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.h264.RtpReceiver
import com.pswidersk.cybereyeapp.model.Status
import com.pswidersk.cybereyeapp.model.StatusResponse
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.serialization.DeserializationStrategy
import kotlinx.serialization.json.Json
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress

private val json = Json { ignoreUnknownKeys = true }

class VideoViewModel : ViewModel() {

    private lateinit var controlSocket: DatagramSocket
    private lateinit var videoSocket: DatagramSocket
    private lateinit var rtpReceiver: RtpReceiver
    private var stopJob: Job? = null

    fun initCommunication() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (!::controlSocket.isInitialized || controlSocket.isClosed) {
                    controlSocket = DatagramSocket().apply {
                        soTimeout = 2000
                    }
                }
                if (!::videoSocket.isInitialized || videoSocket.isClosed) {
                    videoSocket = DatagramSocket().apply {
                        soTimeout = 2000
                    }
                }
                if (!::rtpReceiver.isInitialized) {
                    rtpReceiver = RtpReceiver(videoSocket)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error: ${e.message}")
            }
        }
    }

    suspend fun startVideo(h264Decoder: H264Decoder): Boolean = withContext(Dispatchers.IO) {
        val response = requestCommand(
            "start",
            StatusResponse.serializer(),
            StatusResponse(),
            videoSocket
        )
        if (response.status != Status.OK) {
            Log.e(TAG, "Start failed")
            return@withContext false
        }
        rtpReceiver.start { h264Decoder.decode(it) }
        return@withContext true
    }

    suspend fun sendCommand(
        command: String
    ): Boolean = withContext(Dispatchers.IO) {
        val response = requestCommand(command, StatusResponse.serializer(), StatusResponse())
        return@withContext response.status == Status.OK
    }

    suspend fun <T> requestCommand(
        command: String,
        deserializer: DeserializationStrategy<T>,
        default: T,
        sourceSocket: DatagramSocket = controlSocket
    ): T = withContext(Dispatchers.IO) {
        val addr = InetSocketAddress(cameraIp.value, CONTROL_PORT)
        val data = command.toByteArray()

        try {
            sourceSocket.send(DatagramPacket(data, data.size, addr))

            val buf = ByteArray(4096)
            val reply = DatagramPacket(buf, buf.size)
            sourceSocket.receive(reply)

            val raw = String(reply.data, 0, reply.length, Charsets.UTF_8)
            Log.d(TAG, "Command '$command' → $raw")
            json.decodeFromString(deserializer, raw)
        } catch (ex: Exception) {
            Log.e(TAG, "Error response for '$command'", ex)
            default
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
        if (::controlSocket.isInitialized) controlSocket.close()
        if (::videoSocket.isInitialized) videoSocket.close()
        if (::rtpReceiver.isInitialized) rtpReceiver.stop()
        Log.d(TAG, "Socket closed")
    }
}