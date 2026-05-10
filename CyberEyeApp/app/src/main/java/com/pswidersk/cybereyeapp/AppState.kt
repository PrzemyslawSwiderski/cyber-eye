package com.pswidersk.cybereyeapp

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

const val TAG = "CyberEye"
const val ESP32_IP = "192.168.1.17"
const val CONTROL_PORT = 3334

object AppState {

    private val _rtpStats = MutableStateFlow(RtpStats(0f, 0f, 0f, 0f))
    val rtpStats = _rtpStats.asStateFlow()

    fun updateStats(stats: RtpStats) {
        _rtpStats.value = stats
    }
}