#!/usr/bin/env python3
"""
Minimal RTP packetizer for H.264 (RFC 6184).

Supports:
- Single NAL Unit packets (NAL size <= max payload)
- FU-A fragmentation for large NALs (e.g. IDR frames)
"""

import socket
import struct
import time


RTP_VERSION = 2
H264_PAYLOAD_TYPE = 96  # dynamic payload type, must match SDP on receiver side


class RtpH264Packetizer:
    def __init__(self, dest_ip, dest_port, payload_type=H264_PAYLOAD_TYPE,
                 ssrc=None, clock_rate=90000, mtu=1400):
        """
        dest_ip / dest_port: where to send RTP/UDP packets
        payload_type: RTP payload type number (must match SDP)
        ssrc: synchronization source identifier (random if None)
        clock_rate: RTP clock rate for H264 is 90000 Hz per RFC
        mtu: max payload size per RTP packet (excluding RTP header),
             keep below network MTU minus headers (~1400 is safe for UDP)
        """
        self.dest = (dest_ip, dest_port)
        self.payload_type = payload_type
        self.ssrc = ssrc if ssrc is not None else int(time.time()) & 0xFFFFFFFF
        self.clock_rate = clock_rate
        self.mtu = mtu

        self.seq = 0
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def _rtp_header(self, timestamp, marker):
        """Build a 12-byte RTP header."""
        first_byte = (RTP_VERSION << 6)  # version=2, padding=0, extension=0, CC=0
        second_byte = (marker << 7) | self.payload_type
        header = struct.pack(
            "!BBHII",
            first_byte,
            second_byte,
            self.seq & 0xFFFF,
            timestamp & 0xFFFFFFFF,
            self.ssrc,
        )
        self.seq = (self.seq + 1) & 0xFFFF
        return header

    def _send(self, payload, timestamp, marker):
        packet = self._rtp_header(timestamp, marker) + payload
        self.sock.sendto(packet, self.dest)

    def send_nal(self, nal, timestamp):
        """
        Send a single NAL unit (without start code), fragmenting if needed.
        timestamp: 32-bit RTP timestamp (e.g. derived from capture time * clock_rate)
        """
        if len(nal) <= self.mtu:
            # Single NAL Unit packet — marker bit set (last/only packet of this frame)
            self._send(nal, timestamp, marker=1)
        else:
            self._send_fu_a(nal, timestamp)

    def _send_fu_a(self, nal, timestamp):
        """Fragment a large NAL using FU-A (RFC 6184 section 5.8)."""
        nal_header = nal[0]
        nal_type = nal_header & 0x1F
        nri = nal_header & 0x60  # nal_ref_idc bits

        fu_indicator = nri | 28  # FU-A type = 28
        payload = nal[1:]
        max_fragment_size = self.mtu - 2  # 2 bytes for FU header (indicator + header)

        offset = 0
        total = len(payload)
        while offset < total:
            fragment = payload[offset:offset + max_fragment_size]
            is_first = offset == 0
            is_last = (offset + len(fragment)) >= total

            start_bit = 0x80 if is_first else 0x00
            end_bit = 0x40 if is_last else 0x00
            fu_header = start_bit | end_bit | nal_type

            data = bytes([fu_indicator, fu_header]) + fragment
            self._send(data, timestamp, marker=1 if is_last else 0)

            offset += len(fragment)

    def close(self):
        self.sock.close()


def make_timestamp(start_time, clock_rate=90000):
    """Compute a 32-bit RTP timestamp from elapsed wall-clock time."""
    elapsed = time.time() - start_time
    return int(elapsed * clock_rate) & 0xFFFFFFFF
