package com.pswidersk.cybereyeapp

import android.app.Activity
import android.content.pm.ActivityInfo
import android.os.Bundle
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.lifecycleScope
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.ui.screens.VideoScreen
import com.pswidersk.cybereyeapp.ui.theme.CyberEyeAppTheme
import kotlinx.coroutines.launch
import kotlin.math.abs


class VideoActivity : ComponentActivity() {

    private val viewModel: VideoViewModel by viewModels()
    private var h264Decoder: H264Decoder? = null
    private var isReceiving by mutableStateOf(false)
    private var showOverlay by mutableStateOf(true)
    private var brightness by mutableFloatStateOf(0.5f)
    private var quality by mutableFloatStateOf(0.7f)
    private var lastSentQuality = 0.7f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
        enableEdgeToEdge()
        viewModel.initCommunication()

        setContent {
            CyberEyeAppTheme {
                Box(modifier = Modifier.fillMaxSize()) {
                    AndroidView(
                        factory = { ctx ->
                            SurfaceView(ctx).apply {
                                holder.addCallback(object : SurfaceHolder.Callback {
                                    override fun surfaceCreated(h: SurfaceHolder) {
                                        h264Decoder = H264Decoder(h.surface)
                                        startStreaming()
                                    }

                                    override fun surfaceDestroyed(h: SurfaceHolder) {
                                        stopStreaming()
                                        h264Decoder?.release()
                                        h264Decoder = null
                                    }

                                    override fun surfaceChanged(
                                        h: SurfaceHolder,
                                        f: Int,
                                        w: Int,
                                        h2: Int
                                    ) {
                                    }
                                })
                            }
                        },
                        modifier = Modifier.fillMaxSize()
                    )
                    VideoScreen(
                        visible = showOverlay,
                        brightness = brightness,
                        quality = quality,
                        onBrightnessChange = { newBrightness ->
                            brightness = newBrightness
                            setScreenBrightness(newBrightness)
                        },
                        onQualityChange = { newQuality ->
                            quality = newQuality
                            sendQualityCommand(newQuality)
                        },
                    )
                }
            }
        }
    }

    private fun startStreaming() {
        if (isReceiving) return
        isReceiving = true
        lifecycleScope.launch {
            val ok = viewModel.sendUdpCommand("start")
            if (!ok) {
                isReceiving = false; return@launch
            }
            viewModel.rtpReceiver.start { h264Decoder?.decode(it) }
        }
    }

    private fun stopStreaming() {
        if (!isReceiving) return
        isReceiving = false
        lifecycleScope.launch {
            viewModel.rtpReceiver.stop()
            viewModel.sendUdpCommand("stop")
        }
    }

    private fun sendQualityCommand(qualityValue: Float) {
        if (abs(qualityValue - lastSentQuality) < 0.05f) return
        lastSentQuality = qualityValue

        val qualityPercent = (qualityValue * 100).toInt()
        val qualityLevel = when (qualityPercent) {
            in 0..20 -> 10
            in 21..40 -> 30
            in 41..60 -> 50
            in 61..80 -> 70
            else -> 90
        }

        val command = "quality:$qualityLevel"
        Log.d(TAG, "Sending quality command: $command")

        lifecycleScope.launch {
            viewModel.sendUdpCommand(command)
        }
    }

    private fun setScreenBrightness(value: Float) {
        val layoutParams = window.attributes
        layoutParams.screenBrightness = value.coerceIn(0.0f, 1.0f)
        window.attributes = layoutParams
    }

    override fun onStop() {
        super.onStop()
        if (isReceiving) stopStreaming()
    }
}