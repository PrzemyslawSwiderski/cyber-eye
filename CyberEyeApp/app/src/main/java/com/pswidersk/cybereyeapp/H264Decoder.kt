package com.pswidersk.cybereyeapp

import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log
import android.view.Surface
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class H264Decoder(surface: Surface) {
    private val codec: MediaCodec
    private val nalQueue = LinkedBlockingQueue<ByteArray>(120)
    private val startCode = byteArrayOf(0x00, 0x00, 0x00, 0x01)

    @Volatile
    private var isRunning = true

    @Volatile
    private var waitingForIdr = true   // start in recovery until we see the first IDR

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

    // Called by the RTP thread — just enqueues, never touches the codec
    fun decode(nalUnit: ByteArray) {
        val type = nalUnit[0].toInt() and 0x1F
        if (type !in setOf(1, 5, 6, 7, 8, 9)) return
        if (!nalQueue.offer(nalUnit)) {
            Log.w(TAG, "NAL queue full, dropping type=$type")
        }
    }

    private fun feedInput() {
        while (isRunning) {
            val nalUnit = nalQueue.poll(50, TimeUnit.MILLISECONDS) ?: continue
            val type = nalUnit[0].toInt() and 0x1F

            // Always track the latest SPS/PPS so we can prepend them to the next IDR
            if (type == 7) {
                lastSps = nalUnit; continue
            }
            if (type == 8) {
                lastPps = nalUnit; continue
            }

            // While waiting for IDR: discard everything except IDR
            if (waitingForIdr) {
                if (type != 5) continue

                // Got IDR — prepend SPS+PPS and resume
                Log.i(TAG, "IDR received, resyncing codec")
                lastSps?.let { feedToCodec(it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                lastPps?.let { feedToCodec(it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                waitingForIdr = false
            }

            feedToCodec(nalUnit, 0)
        }
    }

    private fun feedToCodec(nalUnit: ByteArray, flags: Int) {
        if (!isRunning) return

        val type = nalUnit[0].toInt() and 0x1F
        val index = try {
            codec.dequeueInputBuffer(100_000L)
        } catch (e: IllegalStateException) {
            if (isRunning) Log.w("Decoder", "dequeueInputBuffer error — flushing", e)
            triggerResync()
            return
        }

        if (index < 0) {
            Log.w(TAG, "Codec stall on type=$type — flushing and waiting for next IDR")
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
    }

    private fun triggerResync() {
        waitingForIdr = true
        nalQueue.clear()
        try {
            if (isRunning) codec.flush()
        } catch (e: IllegalStateException) {
            if (isRunning) Log.e("Decoder", "flush error", e)
        }
    }

    private fun drainOutput() {
        val info = MediaCodec.BufferInfo()
        while (isRunning) {
            try {
                val outIndex = codec.dequeueOutputBuffer(info, 10_000L)
                when {
                    outIndex >= 0 -> {
                        val isEos = (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0
                        codec.releaseOutputBuffer(outIndex, true)
                        if (isEos) {
                            Log.d(TAG, "EOS"); break
                        }
                    }

                    outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED ->
                        Log.d(TAG, "Format changed: ${codec.outputFormat}")
                }
            } catch (e: IllegalStateException) {
                if (isRunning) Log.e(TAG, "drainOutput error", e)
                break  // whether it's shutdown or a real error, we can't continue
            } catch (e: Exception) {
                Log.e(TAG, "drainOutput error", e)
                break
            }
        }
    }

    fun release() {
        isRunning = false
        nalQueue.clear()
        codec.stop()
        codec.release()
    }
}