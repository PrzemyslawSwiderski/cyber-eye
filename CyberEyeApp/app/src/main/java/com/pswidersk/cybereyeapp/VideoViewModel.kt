package com.pswidersk.cybereyeapp

import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.h264.RtpReceiver
import com.pswidersk.cybereyeapp.model.Status
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext

class VideoViewModel : ViewModel() {

    private val rtpReceiver = RtpReceiver()
    private var stopJob: Job? = null

    suspend fun startVideo(h264Decoder: H264Decoder): Boolean = withContext(Dispatchers.IO) {
        val response = CameraClient.startVideo()
        if (response.status != Status.OK) return@withContext false
        rtpReceiver.start { h264Decoder.decode(it) }
        true
    }

    fun scheduleStop() {
        stopJob = viewModelScope.launch(Dispatchers.IO) {
            if (!CameraClient.sendCommand("stop")) Log.e(TAG, "Stop failed")
        }
    }

    override fun onCleared() {
        super.onCleared()
        runBlocking { stopJob?.join() }
        rtpReceiver.stop()
    }
}