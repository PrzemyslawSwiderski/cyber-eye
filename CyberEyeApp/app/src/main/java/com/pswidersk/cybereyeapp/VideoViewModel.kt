package com.pswidersk.cybereyeapp

import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.pswidersk.cybereyeapp.AppState.cameraIp
import com.pswidersk.cybereyeapp.h264.RtpReceiver
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress

class VideoViewModel : ViewModel() {

    private lateinit var socket: DatagramSocket

    lateinit var rtpReceiver: RtpReceiver
        private set

    fun initCommunication() {
        // Run in background to avoid NetworkOnMainThreadException
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

    suspend fun sendUdpCommand(command: String): Boolean = withContext(Dispatchers.IO) {
        val addr = InetSocketAddress(cameraIp.value, CONTROL_PORT)
        val data = command.toByteArray()

        try {
            socket.send(DatagramPacket(data, data.size, addr))

            val buf = ByteArray(32)
            val reply = DatagramPacket(buf, buf.size)

            // socket.receive will block this coroutine until data arrives
            // or soTimeout is reached
            socket.receive(reply)

            Log.d(TAG, "Command '$command' → '${String(reply.data, 0, reply.length)}'")
            true // This is the return value for the whole block
        } catch (ex: Exception) {
            Log.e(TAG, "Error response for '$command'", ex)
            false // Return false on timeout or socket error
        }
    }

    override fun onCleared() {
        super.onCleared()
        // This is called when the Activity is finished for good
        if (::socket.isInitialized) {
            socket.close()
        }
        if (::rtpReceiver.isInitialized) {
            rtpReceiver.stop()
        }
        Log.d(TAG, "Socket closed")
    }
}