package com.pswidersk.cybereyeapp

import android.content.pm.ActivityInfo
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.lifecycleScope
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.ui.screens.VideoScreen
import com.pswidersk.cybereyeapp.ui.theme.CyberEyeAppTheme
import kotlinx.coroutines.launch


class VideoActivity : ComponentActivity() {

    private val viewModel: VideoViewModel by viewModels()
    private var h264Decoder: H264Decoder? = null
    private var isReceiving by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
        enableEdgeToEdge()

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
                    VideoScreen(onRestartStream = {
                        stopStreaming()
                        startStreaming()
                    })
                }
            }
        }
    }

    private fun startStreaming() {
        if (isReceiving) return
        isReceiving = true
        lifecycleScope.launch {
            val success = viewModel.startVideo(h264Decoder!!)
            if (!success) {
                isReceiving = false
                return@launch
            }
        }
    }

    private fun stopStreaming() {
        if (!isReceiving) return
        isReceiving = false
        lifecycleScope.launch {
            viewModel.scheduleStop()
        }
    }


    override fun onStop() {
        super.onStop()
        if (isReceiving) stopStreaming()
    }
}