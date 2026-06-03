package com.pswidersk.cybereyeapp

import android.content.ContentValues
import android.content.pm.ActivityInfo
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.view.PixelCopy
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.graphics.createBitmap
import com.pswidersk.cybereyeapp.h264.H264Decoder
import com.pswidersk.cybereyeapp.ui.screens.VideoScreen
import com.pswidersk.cybereyeapp.ui.theme.CyberEyeAppTheme
import com.pswidersk.cybereyeapp.ui.theme.VideoOverlayTheme.Colors.background
import java.io.OutputStream
import java.time.LocalDateTime.now
import java.time.format.DateTimeFormatter

class VideoActivity : ComponentActivity() {

    private val viewModel: VideoViewModel by viewModels()
    private var h264Decoder: H264Decoder? = null
    private var surfaceView: SurfaceView? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE
        enableEdgeToEdge()

        setContent {
            CyberEyeAppTheme {
                val reloadKey by remember { AppState.shouldReloadVideo }

                Box(modifier = Modifier.fillMaxSize()) {
                    key(reloadKey) {
                        AndroidView(
                            factory = { ctx ->
                                SurfaceView(ctx).apply {
                                    surfaceView = this
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
                    VideoScreen(onScreenshot = ::takeScreenshot)
                }
            }
        }
    }

    fun takeScreenshot() {
        val sv = surfaceView ?: run {
            Toast.makeText(this, "Surface not ready", Toast.LENGTH_SHORT).show()
            return
        }

        takeScreenshotPixelCopy(sv)
    }

    private fun takeScreenshotPixelCopy(sv: SurfaceView) {
        val bitmap = createBitmap(sv.width, sv.height)

        PixelCopy.request(sv, bitmap, { result ->
            if (result == PixelCopy.SUCCESS) {
                saveBitmapToGallery(bitmap)
            } else {
                runOnUiThread {
                    Toast.makeText(
                        this,
                        "Screenshot failed (PixelCopy error $result)",
                        Toast.LENGTH_SHORT
                    ).show()
                }
            }
        }, Handler(Looper.getMainLooper()))
    }

    private fun saveBitmapToGallery(bitmap: Bitmap) {
        val dateTime = now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH-mm-ss"))
        val filename = "CyberEye_$dateTime.png"

        val contentValues = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, filename)
            put(MediaStore.Images.Media.MIME_TYPE, "image/png")
            put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/CyberEye")
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }

        val resolver = contentResolver
        val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, contentValues)

        if (uri == null) {
            runOnUiThread {
                Toast.makeText(this, "Failed to create image file", Toast.LENGTH_SHORT).show()
            }
            return
        }

        try {
            val stream: OutputStream = resolver.openOutputStream(uri)!!
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
            stream.flush()
            stream.close()

            contentValues.clear()
            contentValues.put(MediaStore.Images.Media.IS_PENDING, 0)
            resolver.update(uri, contentValues, null, null)

            runOnUiThread {
                Toast.makeText(this, "Screenshot saved to Pictures/CyberEye", Toast.LENGTH_SHORT)
                    .show()
            }
        } catch (e: Exception) {
            resolver.delete(uri, null, null)
            runOnUiThread {
                Toast.makeText(this, "Failed to save screenshot: ${e.message}", Toast.LENGTH_SHORT)
                    .show()
            }
        }
    }
}