package com.pswidersk.cybereyeapp.h264

import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log
import android.view.Surface
import com.pswidersk.cybereyeapp.TAG
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class H264Decoder(surface: Surface) {
    private val codec: MediaCodec
    private val nalQueue = LinkedBlockingQueue<ByteArray>(60)
    private val startCode = byteArrayOf(0x00, 0x00, 0x00, 0x01)

    @Volatile
    private var isRunning = true

    @Volatile
    private var waitingForIdr = true

    private var lastSps: ByteArray? = null
    private var lastPps: ByteArray? = null

    init {
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, 1280, 960)
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 512 * 1024)
        format.setInteger(MediaFormat.KEY_PRIORITY, 0)
        format.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)

        codec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        codec.configure(format, surface, null, 0)
        codec.start()

        Thread { feedInput() }.apply { name = "H264-input"; start() }
        Thread { drainOutput() }.apply { name = "H264-output"; start() }
    }

    fun decode(nalUnit: ByteArray) {
        val type = nalUnit[0].toInt() and 0x1F
        if (type !in setOf(1, 5, 6, 7, 8, 9)) return
        if (!nalQueue.offer(nalUnit)) {
            Log.w(TAG, "NAL queue full, dropping type=$type")
            nalQueue.clear()
        }
    }

    private fun feedInput() {
        while (isRunning) {
            try {
                val nalUnit = nalQueue.poll(50, TimeUnit.MILLISECONDS) ?: continue
                val type = nalUnit[0].toInt() and 0x1F

                when (type) {
                    7 -> {
                        lastSps = nalUnit; continue
                    }

                    8 -> {
                        lastPps = nalUnit; continue
                    }

                    5 -> {
                        // IDR received - prepend SPS+PPS if waiting
                        if (waitingForIdr) {
                            Log.i(TAG, "IDR received, resyncing codec")
                            lastSps?.let { feedToCodec(it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                            lastPps?.let { feedToCodec(it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                            waitingForIdr = false
                        }
                        feedToCodec(nalUnit, 0)
                    }

                    else -> {
                        if (!waitingForIdr) {
                            feedToCodec(nalUnit, 0)
                        }
                    }
                }
            } catch (e: Exception) {
                if (isRunning) Log.e(TAG, "feedInput error", e)
            }
        }
    }

    private fun feedToCodec(nalUnit: ByteArray, flags: Int) {
        try {
            val index = codec.dequeueInputBuffer(10000)
            if (index < 0) {
                if (index == MediaCodec.INFO_TRY_AGAIN_LATER) return
                Log.w(TAG, "dequeueInputBuffer returned $index, triggering resync")
                triggerResync()
                return
            }

            val buf = codec.getInputBuffer(index) ?: return
            buf.clear()
            buf.put(startCode)
            buf.put(nalUnit)
            codec.queueInputBuffer(
                index, 0, startCode.size + nalUnit.size,
                System.nanoTime() / 1000, flags
            )
        } catch (_: IllegalStateException) {
            if (isRunning) {
                Log.w(TAG, "Codec not accepting input, triggering resync")
                triggerResync()
            }
        } catch (e: Exception) {
            if (isRunning) Log.e(TAG, "feedToCodec error", e)
        }
    }

    private fun triggerResync() {
        if (!isRunning) return
        waitingForIdr = true
        nalQueue.clear()
        try {
            codec.flush()
        } catch (_: Exception) {
            // Ignore flush errors during shutdown
        }
    }

    private fun drainOutput() {
        val info = MediaCodec.BufferInfo()
        while (isRunning) {
            try {
                val outIndex = codec.dequeueOutputBuffer(info, 10000)
                when {
                    outIndex >= 0 -> {
                        codec.releaseOutputBuffer(outIndex, true)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) {
                            Log.d(TAG, "EOS received")
                            break
                        }
                    }

                    outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        Log.d(TAG, "Format changed: ${codec.outputFormat}")
                    }
                }
            } catch (e: Exception) {
                if (isRunning) Log.e(TAG, "drainOutput error", e)
                break
            }
        }
    }

    fun release() {
        isRunning = false
        nalQueue.clear()
        try {
            codec.stop()
        } catch (_: Exception) {
            // Ignore
        }
        try {
            codec.release()
        } catch (_: Exception) {
            // Ignore
        }
    }
}