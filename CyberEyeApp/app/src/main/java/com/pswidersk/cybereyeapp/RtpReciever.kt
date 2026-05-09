package com.pswidersk.cybereyeapp

import android.util.Log
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketTimeoutException

private const val TAG = "RtpReceiver"

class RtpReceiver(private val socket: DatagramSocket) {
    private var running = false
    private val fragmentBuffer = java.io.ByteArrayOutputStream()
    private var fuStarted = false

    fun start(onNalUnit: (ByteArray) -> Unit) {
        running = true

        Thread {
            val buf = ByteArray(65535)
            val packet = DatagramPacket(buf, buf.size)

            while (running && socket.isBound && !socket.isClosed) {
                try {
                    socket.receive(packet)
                    val nalUnits = parseRtp(packet.data, packet.length)
                    nalUnits.forEach { onNalUnit(it) }
                } catch (_: SocketTimeoutException) {
                    Log.w(TAG, "Socket timeout")
                } catch (e: Exception) {
                    if (running) Log.e(TAG, "Receive error", e)
                }
            }
        }.start()
    }

    private fun parseRtp(data: ByteArray, length: Int): List<ByteArray> {
        if (length < 12) return emptyList()

        // ── Resolve true payload offset ──────────────────────────────────────
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

        // ── Parse NAL / RTP payload type ─────────────────────────────────────
        val payload = data.copyOfRange(offset, length)
        val nalHeader = payload[0].toInt() and 0xFF

        return when (val naluType = nalHeader and 0x1F) {

            // ── Single NAL unit (types 1–23) ─────────────────────────────────
            in 1..23 -> {
                Log.d(TAG, "Single NAL type=$naluType size=${payload.size}")
                listOf(payload)
            }

            // ── STAP-A (type 24): multiple NALs in one packet ─────────────────
            24 -> parseStapA(payload)

            // ── FU-A (type 28): fragmented NAL ───────────────────────────────
            28 -> parseFuA(payload, nalHeader)

            else -> {
                Log.w(TAG, "Unhandled RTP NAL type=$naluType — skipping")
                emptyList()
            }
        }
    }

    // STAP-A layout: [ 24 | size(2B) | NAL | size(2B) | NAL | ... ]
    private fun parseStapA(payload: ByteArray): List<ByteArray> {
        val nals = mutableListOf<ByteArray>()
        var i = 1 // skip STAP-A header byte

        while (i + 2 <= payload.size) {
            val naluSize = ((payload[i].toInt() and 0xFF) shl 8) or
                    (payload[i + 1].toInt() and 0xFF)
            i += 2
            if (naluSize <= 0 || i + naluSize > payload.size) {
                Log.w(TAG, "STAP-A: invalid NAL size=$naluSize at offset=$i — aborting parse")
                break
            }
            nals.add(payload.copyOfRange(i, i + naluSize))
            i += naluSize
        }

        Log.d(TAG, "STAP-A: extracted ${nals.size} NAL(s)")
        return nals
    }

    // FU-A layout: [ FU indicator | FU header | payload... ]
    //   FU indicator: [ F | NRI | 28 ]
    //   FU header:    [ S | E | R | nal_unit_type ]
    private fun parseFuA(payload: ByteArray, nalHeader: Int): List<ByteArray> {
        if (payload.size < 2) return emptyList()

        val fuHeader = payload[1].toInt() and 0xFF
        val startBit = (fuHeader and 0x80) != 0
        val endBit = (fuHeader and 0x40) != 0
        val actualType = fuHeader and 0x1F

        if (startBit) {
            fragmentBuffer.reset()
            fuStarted = true
            // Reconstruct original NAL header: preserve F+NRI from FU indicator, use actual type
            val reconstructedHeader = (nalHeader and 0xE0) or actualType
            fragmentBuffer.write(reconstructedHeader)
            Log.d(TAG, "FU-A start: reconstructed NAL type=$actualType")
        }

        if (!fuStarted) {
            Log.w(TAG, "FU-A middle/end fragment arrived without start — discarding")
            return emptyList()
        }

        // Append fragment body (skip the 2-byte FU-A header)
        fragmentBuffer.write(payload, 2, payload.size - 2)

        return if (endBit) {
            fuStarted = false
            val assembled = fragmentBuffer.toByteArray()
            Log.d(TAG, "FU-A end: assembled NAL type=$actualType size=${assembled.size}")
            listOf(assembled)
        } else {
            emptyList() // middle fragment — keep accumulating
        }
    }

    fun stop() {
        running = false
    }
}