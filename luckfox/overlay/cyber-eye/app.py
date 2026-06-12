#!/usr/bin/env python3
import time
import sys
import subprocess
import logging
from logging.handlers import RotatingFileHandler

from h264_reader import H264FifoReader, is_frame_nal
from rtp_h264 import RtpH264Packetizer, make_timestamp


FIFO = "/tmp/venc.h264"

# RTP destination - adjust as needed
DEST_IP = "192.168.1.12"
DEST_PORT = 5004

handler = RotatingFileHandler("/cyber-eye/cyber-eye.log", maxBytes=256_000, backupCount=1)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[handler],
)
logger = logging.getLogger(__name__)

class FpsCounter:
    def __init__(self, report_interval=1.0):
        self.report_interval = report_interval
        self.total_bytes = 0
        self.frame_count = 0
        self.start_time = time.time()
        self.last_report = self.start_time

    def update(self, nal_size, is_frame):
        self.total_bytes += nal_size
        if is_frame:
            self.frame_count += 1

    def maybe_report(self):
        now = time.time()
        elapsed = now - self.last_report
        if elapsed >= self.report_interval:
            fps = self.frame_count / elapsed if elapsed > 0 else 0
            kbps = (self.total_bytes * 8 / 1000) / (now - self.start_time) \
                if (now - self.start_time) > 0 else 0
            logger.info(f"FPS: {fps:.2f} | total_bytes: {self.total_bytes} | "
                  f"avg_bitrate: {kbps:.1f} kbps")
            self.frame_count = 0
            self.last_report = now


def wait_for_network(check_ip="192.168.1.1"):
    """Block until we can reach check_ip (or forever if timeout is None)."""
    while True:
        result = subprocess.run(
            ["ping", "-c", "1", "-W", "1", check_ip],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        if result.returncode == 0:
            return True
        
        logger.info(f"Waiting for network...")
        time.sleep(1)
        
def main():
    wait_for_network()
    logger.info(f"Opening {FIFO} for reading...")
    rtp = RtpH264Packetizer(DEST_IP, DEST_PORT)
    fps = FpsCounter()
    start_time = time.time()

    try:
        with H264FifoReader(FIFO) as reader:
            logger.info("Reader attached. Waiting for data...")
            for nal, nal_type in reader.nals():
                fps.update(len(nal), is_frame_nal(nal_type))

                timestamp = make_timestamp(start_time)
                rtp.send_nal(nal, timestamp)

                fps.maybe_report()
    except KeyboardInterrupt:
        pass
    finally:
        rtp.close()


if __name__ == "__main__":
    sys.exit(main())
