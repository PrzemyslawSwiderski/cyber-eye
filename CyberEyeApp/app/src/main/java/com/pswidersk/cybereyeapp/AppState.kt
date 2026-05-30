package com.pswidersk.cybereyeapp

import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import com.pswidersk.cybereyeapp.h264.RtpStats
import com.pswidersk.cybereyeapp.model.StatsResponse
import com.pswidersk.cybereyeapp.model.Status
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

const val TAG = "CyberEye"
const val DEFAULT_IP = "192.168.1.17"
const val ACCESS_POINT_IP = "192.168.4.1"
const val CONTROL_PORT = 3334

object AppState {

    val cameraIp = mutableStateOf(DEFAULT_IP)
    private val _rtpStats = MutableStateFlow(RtpStats(0f, 0f, 0f, 0f))
    val rtpStats = _rtpStats.asStateFlow()
    val cameraStatus = mutableStateOf(Status.PENDING)
    val cameraStats = MutableStateFlow(StatsResponse())
    val shouldReloadVideo = mutableIntStateOf(0)

    fun requestVideoReload() {
        shouldReloadVideo.intValue++
    }

    fun updateStats(stats: RtpStats) {
        _rtpStats.value = stats
    }
}