

#!/usr/bin/env python3
"""
Reads an H.264 Annex B elementary stream from a FIFO and yields complete NAL units.
"""

import os
import time


# NAL unit types of interest (Table 7-1, H.264 spec)
NAL_TYPE_SLICE_NON_IDR = 1
NAL_TYPE_SLICE_IDR = 5
NAL_TYPE_SPS = 7
NAL_TYPE_PPS = 8

FRAME_NAL_TYPES = (NAL_TYPE_SLICE_NON_IDR, NAL_TYPE_SLICE_IDR)

START_CODE = b"\x00\x00\x00\x01"


class H264FifoReader:
    def __init__(self, fifo_path, chunk_size=65536):
        self.fifo_path = fifo_path
        self.chunk_size = chunk_size
        self._buf = b""
        self._fh = None

    def open(self):
        if not os.path.exists(self.fifo_path):
            os.mkfifo(self.fifo_path)
        self._fh = open(self.fifo_path, "rb")
        return self

    def close(self):
        if self._fh:
            self._fh.close()
            self._fh = None

    def __enter__(self):
        return self.open()

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def nals(self):
        if self._fh is None:
            self.open()

        pos = 0
        while True:
            chunk = self._fh.read(self.chunk_size)
            if not chunk:
                time.sleep(0.001)
                continue

            self._buf += chunk

            while True:
                start = self._buf.find(START_CODE, pos)
                if start == -1:
                    break
                next_start = self._buf.find(START_CODE, start + 4)
                if next_start == -1:
                    break
                nal = self._buf[start + 4:next_start]
                if nal:
                    nal_type = nal[0] & 0x1F
                    yield nal, nal_type
                pos = next_start

            # trim processed data to avoid unbounded buffer growth
            if pos > 0:
                self._buf = self._buf[pos:]
                pos = 0

def is_frame_nal(nal_type):
    return nal_type in FRAME_NAL_TYPES
