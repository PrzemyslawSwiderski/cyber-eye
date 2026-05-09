package com.pswidersk.cybereyeapp

import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.lifecycleScope
import com.pswidersk.cybereyeapp.ui.theme.CyberEyeAppTheme
import kotlinx.coroutines.launch

class VideoActivity : ComponentActivity() {

    companion object {
        private const val TAG = "CyberEyeVideo"
    }

    private var isReceiving by mutableStateOf(false)
    private var statusText by mutableStateOf("Idle")
    private var h264Decoder: H264Decoder? = null

    private val viewModel: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        enableEdgeToEdge()

        viewModel.initCommunication()

        setContent {
            CyberEyeAppTheme {
                VideoScreen(
                    onSettingsClick = {
                        finish() // Go back to settings
                    },
                    onSurfaceReady = { surface ->
                        h264Decoder = H264Decoder(surface)
                        startStreaming()
                    },
                    onSurfaceDestroyed = {
                        stopStreaming()
                        h264Decoder?.release()
                        h264Decoder = null
                    }
                )
            }
        }
    }

    private fun startStreaming() {
        if (isReceiving) return
        Log.d(TAG, "Starting video stream...")
        isReceiving = true
        statusText = "Connecting…"

        lifecycleScope.launch {
            val ok = viewModel.sendUdpCommand("start")
            if (!ok) {
                statusText = "❌ Command failed"
                isReceiving = false
                return@launch
            }

            statusText = "▶ Receiving"

            viewModel.rtpReceiver.start { nalUnit ->
                h264Decoder?.decode(nalUnit)
            }
        }
    }

    private fun stopStreaming() {
        if (!isReceiving) return
        isReceiving = false
        statusText = "Idle"
        lifecycleScope.launch {
            viewModel.rtpReceiver.stop()
            viewModel.sendUdpCommand("stop")
        }
    }

    override fun onStop() {
        super.onStop()
        if (isReceiving) stopStreaming()
    }
}

@Composable
fun VideoScreen(
    onSettingsClick: () -> Unit,
    onSurfaceReady: (Surface) -> Unit,
    onSurfaceDestroyed: () -> Unit
) {
    Box(modifier = Modifier.fillMaxSize()) {
        // Full screen video surface
        AndroidView(
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(h: SurfaceHolder) = onSurfaceReady(h.surface)
                        override fun surfaceDestroyed(h: SurfaceHolder) = onSurfaceDestroyed()
                        override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, h2: Int) {}
                    })
                }
            },
            modifier = Modifier.fillMaxSize()
        )

        // Settings button overlay
        IconButton(
            onClick = onSettingsClick,
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(16.dp)
        ) {
            Icon(
                imageVector = Icons.Default.Settings,
                contentDescription = "Settings",
                tint = androidx.compose.ui.graphics.Color.White
            )
        }
    }
}