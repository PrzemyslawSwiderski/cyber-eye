package com.pswidersk.cybereyeapp

import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import com.pswidersk.cybereyeapp.h264.RtpStats
import com.pswidersk.cybereyeapp.model.CameraInfoResponse
import com.pswidersk.cybereyeapp.model.CameraStatus
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

const val TAG = "CyberEye"
const val DEFAULT_IP = "192.168.1.17"
const val ACCESS_POINT_IP = "192.168.4.1"
const val CONTROL_PORT = 3334

data class CameraSettings(
    val exposure: Float = DEFAULT_EXPOSURE,
    val quality: Float = DEFAULT_QUALITY,
) {
    companion object {
        const val DEFAULT_EXPOSURE = 80f
        const val DEFAULT_QUALITY = 46f
        val DEFAULTS = CameraSettings()
    }

    fun toCommand(): String {
        return "camera:::qual:${quality.toInt()}:::exp:${exposure.toInt()}"
    }
}

object AppState {

    val cameraIp = mutableStateOf(DEFAULT_IP)
    private val _rtpStats = MutableStateFlow(RtpStats(0f, 0f, 0f, 0f))
    val rtpStats = _rtpStats.asStateFlow()
    val cameraInfo = MutableStateFlow(CameraInfoResponse())

    val cameraLatency = mutableLongStateOf(0L)

    // Video reload trigger
    val shouldReloadVideo = mutableIntStateOf(0)

    // Camera settings as a single state object
    val cameraSettings = mutableStateOf(CameraSettings.DEFAULTS)

    fun requestVideoReload() {
        shouldReloadVideo.intValue++
    }

    fun updateStats(stats: RtpStats) {
        _rtpStats.value = stats
    }

    // Convenience methods for updating individual settings
    fun updateExposure(value: Float) {
        cameraSettings.value = cameraSettings.value.copy(exposure = value)
    }

    fun updateQuality(value: Float) {
        cameraSettings.value = cameraSettings.value.copy(quality = value)
    }
}