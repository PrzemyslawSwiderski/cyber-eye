package com.pswidersk.cybereyeapp

import android.util.Log
import com.pswidersk.cybereyeapp.AppState.cameraIp
import com.pswidersk.cybereyeapp.model.Status
import com.pswidersk.cybereyeapp.model.StatusResponse
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.DeserializationStrategy
import kotlinx.serialization.json.Json
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress

private val json = Json { ignoreUnknownKeys = true }

object CameraClient {

    private val controlSocket = DatagramSocket().apply { soTimeout = 2000 }
    private val videoSocket = DatagramSocket().apply { soTimeout = 2000 }

    suspend fun sendCommand(command: String): Boolean {
        val response = requestCommand(command, StatusResponse.serializer(), StatusResponse())
        return response.status == Status.OK
    }

    suspend fun startVideo(): StatusResponse = withContext(Dispatchers.IO) {
        // stop any existing stream
        requestCommand(
            "stop",
            StatusResponse.serializer(),
            StatusResponse(),
            controlSocket
        )
        val startResponse: StatusResponse = requestCommand(
            "start",
            StatusResponse.serializer(),
            StatusResponse(),
            videoSocket
        )
        return@withContext startResponse
    }


    fun fillVideoBuffer(packet: DatagramPacket) {
        return videoSocket.receive(packet)
    }

    suspend fun <T> requestCommand(
        command: String,
        deserializer: DeserializationStrategy<T>,
        default: T,
        sourceSocket: DatagramSocket? = null
    ): T = withContext(Dispatchers.IO) {
        val socket = sourceSocket ?: controlSocket
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
            default
        }
    }
}