package com.pswidersk.cybereyeapp

import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.h264.RtpReceiver
import com.pswidersk.cybereyeapp.model.CameraStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

class VideoViewModel : ViewModel() {

    private val rtpReceiver = RtpReceiver()
    private var isStreaming = false

    fun startVideo(decoder: H264Decoder) {
        if (isStreaming) return
        isStreaming = true

        viewModelScope.launch(Dispatchers.IO) {
            CameraClient.startVideo()
            rtpReceiver.start { decoder.decode(it) }
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

}