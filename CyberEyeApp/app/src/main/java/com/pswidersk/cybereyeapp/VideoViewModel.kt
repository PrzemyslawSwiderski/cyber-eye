package com.pswidersk.cybereyeapp

import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.h264.RtpReceiver
import com.pswidersk.cybereyeapp.model.Status
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

class VideoViewModel : ViewModel() {

    private val rtpReceiver = RtpReceiver()
    private var isStreaming = false

    fun startVideo(decoder: H264Decoder) {
        if (isStreaming) return
        isStreaming = true

        viewModelScope.launch(Dispatchers.IO) {
            val response = CameraClient.startVideo()
            if (response.status == Status.OK) {
                rtpReceiver.start { decoder.decode(it) }
            } else {
                isStreaming = false
                Log.e(TAG, "Failed to start video: ${response.status}")
            }
        }
    }

    fun stopVideo() {
        if (!isStreaming) return
        isStreaming = false

        viewModelScope.launch(Dispatchers.IO) {
            CameraClient.sendCommand("stop")
            rtpReceiver.stop()
        }
    }

    override fun onCleared() {
        super.onCleared()
        viewModelScope.launch(Dispatchers.IO) {
            if (isStreaming) {
                CameraClient.sendCommand("stop")
                rtpReceiver.stop()
            }
        }
    }
}