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
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.ui.screens.VideoScreen
import com.pswidersk.cybereyeapp.ui.theme.CyberEyeAppTheme

class VideoActivity : ComponentActivity() {

    private val viewModel: VideoViewModel by viewModels()
    private var h264Decoder: H264Decoder? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
        enableEdgeToEdge()

        setContent {
            CyberEyeAppTheme {
                val reloadKey by remember { AppState.shouldReloadVideo }

                Box(modifier = Modifier.fillMaxSize()) {
                    // Use key() composable to force recreation
                    key(reloadKey) {
                        AndroidView(
                            factory = { ctx ->
                                SurfaceView(ctx).apply {
                                    holder.addCallback(object : SurfaceHolder.Callback {
                                        override fun surfaceCreated(holder: SurfaceHolder) {
                                            h264Decoder = H264Decoder(holder.surface)
                                            viewModel.startVideo(h264Decoder!!)
                                        }

                                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                                            viewModel.stopVideo()
                                            h264Decoder?.release()
                                            h264Decoder = null
                                        }

                                        override fun surfaceChanged(
                                            holder: SurfaceHolder,
                                            format: Int,
                                            width: Int,
                                            height: Int
                                        ) {
                                        }
                                    })
                                }
                            },
                            modifier = Modifier.fillMaxSize()
                        )
                    }

                    VideoScreen()
                }
            }
        }
    }

    override fun onStop() {
        super.onStop()
        viewModel.stopVideo()
    }
}