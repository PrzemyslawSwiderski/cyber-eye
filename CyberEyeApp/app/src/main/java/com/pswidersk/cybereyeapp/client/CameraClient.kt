package com.pswidersk.cybereyeapp.client

import android.util.Log
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.AppState.cameraIp
import com.pswidersk.cybereyeapp.CONTROL_PORT
import com.pswidersk.cybereyeapp.TAG
import com.pswidersk.cybereyeapp.model.CameraInfoResponse
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.DeserializationStrategy
import kotlinx.serialization.json.Json
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress

private val json = Json {
    ignoreUnknownKeys = true
    coerceInputValues = true
}

private const val DEFAULT_TIMEOUT = 2000

object CameraClient {

    private val controlSocket = DatagramSocket().apply { soTimeout = DEFAULT_TIMEOUT }

    @Volatile
    private var videoSocket = DatagramSocket().apply { soTimeout = DEFAULT_TIMEOUT }
    private val videoSocketLock = Any()

    private fun resetVideoSocket(): DatagramSocket {
        synchronized(videoSocketLock) {
            videoSocket.close()
            return DatagramSocket().apply { soTimeout = DEFAULT_TIMEOUT }.also { videoSocket = it }
        }
    }

    suspend fun sendCommand(
        command: String,
        sourceSocket: DatagramSocket = controlSocket
    ) = withContext(Dispatchers.IO) {
        val addr = InetSocketAddress(cameraIp.value, CONTROL_PORT)
        val data = command.toByteArray()

        try {
            sourceSocket.send(DatagramPacket(data, data.size, addr))
        } catch (ex: Exception) {
            Log.e(TAG, "Error response for '$command'", ex)
        }
    }

    suspend fun startVideo() = withContext(Dispatchers.IO) {
        val freshVideoSocket = resetVideoSocket()
        sendCommand(
            "start",
            freshVideoSocket
        )
    }

    suspend fun fetchCameraInfo() {
        val sentAt = System.currentTimeMillis()
        val cameraInfoResponse = requestCommand(
            "info",
            CameraInfoResponse.serializer(),
            CameraInfoResponse()
        )
        val latencyMs = System.currentTimeMillis() - sentAt
        AppState.cameraInfo.value = cameraInfoResponse
        AppState.cameraLatency.longValue = latencyMs
    }

    fun fillVideoBuffer(packet: DatagramPacket) {
        return videoSocket.receive(packet)
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
}