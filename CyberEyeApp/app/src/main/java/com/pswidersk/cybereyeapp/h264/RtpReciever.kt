package com.pswidersk.cybereyeapp.h264

import android.util.Log
import com.pswidersk.cybereyeapp.AppState
import com.pswidersk.cybereyeapp.TAG
import java.io.ByteArrayOutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketTimeoutException
import kotlin.math.abs

data class RtpStats(
    val bitrateKbps: Float,
    var packetLossPct: Float,
    val jitterMs: Float,
    val fps: Float
)

class RtpReceiver(
    private val socket: DatagramSocket
) {

    private var running = false
    private val fragmentBuffer = ByteArrayOutputStream()
    private var fuStarted = false

    // Sequence tracking
    private var lastSeq = -1
    private var lostPackets = 0
    private var totalPackets = 0

    // Bitrate tracking
    private var bytesInWindow = 0L
    private var windowStartMs = System.currentTimeMillis()

    // RFC 3550 jitter (in RTP timestamp units = 90kHz)
    private var lastArrivalTs = -1L   // wall-clock in 90kHz units
    private var lastRtpTs = -1L
    private var jitter = 0.0          // in 90kHz units

    // FPS from RTP timestamps
    private var lastFrameRtpTs = -1L
    private var frameCount = 0
    private var fpsWindowStartMs = System.currentTimeMillis()
    private var currentFps = 0f

    fun start(onNalUnit: (ByteArray) -> Unit) {
        running = true

        Thread {
            val buf = ByteArray(65535)
            val packet = DatagramPacket(buf, buf.size)

            while (running && socket.isBound && !socket.isClosed) {
                try {
                    socket.receive(packet)
                    processPacket(packet, onNalUnit)
                } catch (_: SocketTimeoutException) {
                    Log.w(TAG, "Socket timeout")
                } catch (e: Exception) {
                    if (running) Log.e(TAG, "Receive error", e)
                }
            }
        }.start()
    }

    private fun processPacket(packet: DatagramPacket, onNalUnit: (ByteArray) -> Unit) {
        val data = packet.data
        val length = packet.length
        if (length < 12) return

        val nowMs = System.currentTimeMillis()

        // ── Extract RTP header fields ─────────────────────────────────────────
        val seq = ((data[2].toInt() and 0xFF) shl 8) or (data[3].toInt() and 0xFF)
        val rtpTs = ((data[4].toInt() and 0xFF).toLong() shl 24) or
                ((data[5].toInt() and 0xFF).toLong() shl 16) or
                ((data[6].toInt() and 0xFF).toLong() shl 8) or
                (data[7].toInt() and 0xFF).toLong()

        // ── Sequence / packet loss ────────────────────────────────────────────
        totalPackets++
        if (lastSeq >= 0) {
            val expected = (lastSeq + 1) and 0xFFFF
            if (seq != expected) {
                val gap = (seq - expected + 0x10000) and 0xFFFF
                if (gap < 0x8000) lostPackets += gap   // forward gap = loss
            }
        }
        lastSeq = seq

        // ── Bitrate ───────────────────────────────────────────────────────────
        bytesInWindow += length

        // ── RFC 3550 jitter ───────────────────────────────────────────────────
        // Convert wall-clock arrival to 90kHz units to match RTP timestamp scale
        val arrivalTs90k = nowMs * 90L  // ms → 90kHz (× 90)
        if (lastArrivalTs >= 0) {
            val sendDiff = rtpTs - lastRtpTs
            val arrivalDiff = arrivalTs90k - lastArrivalTs
            val d = abs(arrivalDiff - sendDiff).toDouble()
            jitter += (d - jitter) / 16.0   // RFC 3550 §6.4.1
        }
        lastArrivalTs = arrivalTs90k
        lastRtpTs = rtpTs

        // ── FPS from RTP timestamp transitions ───────────────────────────────
        // A new RTP timestamp = a new frame (even if split across multiple packets)
        if (rtpTs != lastFrameRtpTs) {
            lastFrameRtpTs = rtpTs
            frameCount++
        }

        // ── Emit stats every second ───────────────────────────────────────────
        val elapsedMs = nowMs - windowStartMs
        if (elapsedMs >= 1000) {
            val bitrateKbps = (bytesInWindow * 8f) / elapsedMs  // bits/ms = kbps
            val lossPct = if (totalPackets > 0)
                lostPackets * 100f / (totalPackets + lostPackets) else 0f
            val jitterMs = jitter.toFloat() / 90f               // 90kHz → ms

            val fpsElapsedMs = nowMs - fpsWindowStartMs
            currentFps = if (fpsElapsedMs > 0) frameCount * 1000f / fpsElapsedMs else 0f

            AppState.updateStats(RtpStats(bitrateKbps, lossPct, jitterMs, currentFps))

            // Reset window
            bytesInWindow = 0
            lostPackets = 0
            totalPackets = 0
            frameCount = 0
            windowStartMs = nowMs
            fpsWindowStartMs = nowMs
        }

        // ── Parse and deliver NAL units ───────────────────────────────────────
        parseRtp(data, length).forEach { onNalUnit(it) }
    }

    private fun parseRtp(data: ByteArray, length: Int): List<ByteArray> {
        if (length < 12) return emptyList()

        val b0 = data[0].toInt() and 0xFF
        val hasExtension = (b0 and 0x10) != 0
        val csrcCount = (b0 and 0x0F)

        var offset = 12 + csrcCount * 4

        if (hasExtension) {
            if (offset + 4 > length) return emptyList()
            val extWordLen = ((data[offset + 2].toInt() and 0xFF) shl 8) or
                    (data[offset + 3].toInt() and 0xFF)
            offset += 4 + extWordLen * 4
        }

        if (offset >= length) return emptyList()

        val payload = data.copyOfRange(offset, length)
        val nalHeader = payload[0].toInt() and 0xFF
        return when (val naluType = nalHeader and 0x1F) {
            in 1..23 -> {
                Log.d(TAG, "Single NAL type=$naluType size=${payload.size}")
                listOf(payload)
            }

            24 -> parseStapA(payload)
            28 -> parseFuA(payload, nalHeader)
            else -> {
                Log.w(TAG, "Unhandled RTP NAL type=$naluType — skipping")
                emptyList()
            }
        }
    }

    private fun parseStapA(payload: ByteArray): List<ByteArray> {
        val nals = mutableListOf<ByteArray>()
        var i = 1
        while (i + 2 <= payload.size) {
            val naluSize = ((payload[i].toInt() and 0xFF) shl 8) or
                    (payload[i + 1].toInt() and 0xFF)
            i += 2
            if (naluSize <= 0 || i + naluSize > payload.size) break
            nals.add(payload.copyOfRange(i, i + naluSize))
            i += naluSize
        }
        Log.d(TAG, "STAP-A: extracted ${nals.size} NAL(s)")
        return nals
    }

    private fun parseFuA(payload: ByteArray, nalHeader: Int): List<ByteArray> {
        if (payload.size < 2) return emptyList()
        val fuHeader = payload[1].toInt() and 0xFF
        val startBit = (fuHeader and 0x80) != 0
        val endBit = (fuHeader and 0x40) != 0
        val actualType = fuHeader and 0x1F

        if (startBit) {
            fragmentBuffer.reset()
            fuStarted = true
            fragmentBuffer.write((nalHeader and 0xE0) or actualType)
            Log.d(TAG, "FU-A start: reconstructed NAL type=$actualType")
        }

        if (!fuStarted) {
            Log.w(TAG, "FU-A middle/end fragment arrived without start — discarding")
            return emptyList()
        }

        fragmentBuffer.write(payload, 2, payload.size - 2)

        return if (endBit) {
            fuStarted = false
            val assembled = fragmentBuffer.toByteArray()
            Log.d(TAG, "FU-A end: assembled NAL type=$actualType size=${assembled.size}")
            listOf(assembled)
        } else emptyList()
    }

    fun stop() {
        running = false
    }
}