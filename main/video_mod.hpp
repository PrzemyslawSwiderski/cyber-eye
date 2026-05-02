/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#ifndef V4L2_H264_SIMPLE_HPP
#define V4L2_H264_SIMPLE_HPP

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "esp_log.h"
#include "esp_err.h"

#define H264_DEVICE_PATH "/dev/video11"
#define BUFFER_COUNT 2
// #define FRAME_TIMEOUT_MS 5000
#define FRAME_TIMEOUT_MS 17 // ~60 FPS

static const char *H264_TAG = "V4L2_H264";

class V4L2H264Capture
{
public:
  struct Config
  {
    const char *capture_device = "/dev/video0"; // Camera input device
    int width = 1920;
    int height = 1080;
    int bitrate = 4000000; // 4 Mbps
    int i_period = 30;     // I-frame every 30 frames
    int min_qp = 10;       // Minimum quality
    int max_qp = 40;       // Maximum quality
    bool verbose = true;
  };

  struct Frame
  {
    uint8_t *data;
    size_t size;
    uint32_t sequence;
  };

  explicit V4L2H264Capture(const Config &config) : config_(config), cap_fd_(-1), enc_fd_(-1)
  {
    cap_buffer_[0] = cap_buffer_[1] = nullptr;
    enc_buffer_ = nullptr;
  }

  ~V4L2H264Capture()
  {
    stop();
    cleanup();
  }

  // Initialize capture and encode devices
  esp_err_t init()
  {
    // Open capture device (camera)
    cap_fd_ = open(config_.capture_device, O_RDWR);
    if (cap_fd_ < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to open capture device: %s", config_.capture_device);
      return ESP_FAIL;
    }
    if (config_.verbose)
    {
      printDeviceInfo(cap_fd_, "Capture Device");
    }

    // Open H264 encoder device
    enc_fd_ = open(H264_DEVICE_PATH, O_RDWR);
    if (enc_fd_ < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to open encoder device: %s", H264_DEVICE_PATH);
      return ESP_FAIL;
    }
    if (config_.verbose)
    {
      printDeviceInfo(enc_fd_, "Encoder Device");
    }

    // Configure H264 encoder parameters
    if (!configureEncoder())
    {
      ESP_LOGE(H264_TAG, "Failed to configure encoder");
      return ESP_FAIL;
    }

    return ESP_OK;
  }

  // Start streaming and encoding
  esp_err_t start()
  {
    // Setup capture device
    if (!setupCapture())
    {
      ESP_LOGE(H264_TAG, "Failed to setup capture");
      return ESP_FAIL;
    }

    // Setup encoder input (output from camera)
    if (!setupEncoderOutput())
    {
      ESP_LOGE(H264_TAG, "Failed to setup encoder output");
      return ESP_FAIL;
    }

    // Setup encoder capture (encoded H264 frames)
    if (!setupEncoderCapture())
    {
      ESP_LOGE(H264_TAG, "Failed to setup encoder capture");
      return ESP_FAIL;
    }

    // Start streaming
    if (!startStreaming())
    {
      ESP_LOGE(H264_TAG, "Failed to start streaming");
      return ESP_FAIL;
    }

    ESP_LOGI(H264_TAG, "H264 capture started: %dx%d @ %d bps",
             config_.width, config_.height, config_.bitrate);
    return ESP_OK;
  }

  // Capture a single H264 encoded frame
  bool captureFrame(Frame &frame)
  {
    if (cap_fd_ < 0 || enc_fd_ < 0)
    {
      ESP_LOGE(H264_TAG, "Devices not initialized");
      return false;
    }

    struct v4l2_buffer cap_buf = {};
    cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cap_buf.memory = V4L2_MEMORY_MMAP;

    // Dequeue frame from camera
    if (!dequeueBuffer(cap_fd_, &cap_buf, FRAME_TIMEOUT_MS))
    {
      return false;
    }

    // Send frame to encoder
    struct v4l2_buffer enc_out_buf = {};
    enc_out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_buf.memory = V4L2_MEMORY_USERPTR;
    enc_out_buf.m.userptr = (unsigned long)cap_buffer_[cap_buf.index];
    enc_out_buf.length = cap_buf.bytesused;

    if (ioctl(enc_fd_, VIDIOC_QBUF, &enc_out_buf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to queue buffer to encoder: %s", strerror(errno));
      return false;
    }

    // Get encoded frame from encoder
    struct v4l2_buffer enc_cap_buf = {};
    enc_cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(enc_fd_, VIDIOC_DQBUF, &enc_cap_buf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to dequeue encoded frame: %s", strerror(errno));
      return false;
    }

    // Send camera buffer back to queue
    if (ioctl(cap_fd_, VIDIOC_QBUF, &cap_buf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to requeue camera buffer: %s", strerror(errno));
    }

    // Dequeue the output buffer from encoder
    struct v4l2_buffer enc_out_debuf = {};
    enc_out_debuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_debuf.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(enc_fd_, VIDIOC_DQBUF, &enc_out_debuf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to dequeue encoder output buffer: %s", strerror(errno));
    }

    // Return frame data
    frame.data = enc_buffer_;
    frame.size = enc_cap_buf.bytesused;
    frame.sequence = enc_cap_buf.sequence;

    // Requeue encoder capture buffer
    struct v4l2_buffer enc_cap_qbuf = {};
    enc_cap_qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_qbuf.memory = V4L2_MEMORY_MMAP;
    enc_cap_qbuf.index = 0;
    if (ioctl(enc_fd_, VIDIOC_QBUF, &enc_cap_qbuf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to requeue encoder capture buffer: %s", strerror(errno));
    }

    if (config_.verbose)
    {
      ESP_LOGI(H264_TAG, "Frame %u: %zu bytes", frame.sequence, frame.size);
    }

    return true;
  }

  // Convenience method - returns pointer to internal buffer
  uint8_t *captureFrame(size_t &out_size)
  {
    Frame frame;
    if (captureFrame(frame))
    {
      out_size = frame.size;
      return frame.data;
    }
    out_size = 0;
    return nullptr;
  }

  void stop()
  {
    if (cap_fd_ >= 0)
    {
      int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(cap_fd_, VIDIOC_STREAMOFF, &type);
    }
    if (enc_fd_ >= 0)
    {
      int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(enc_fd_, VIDIOC_STREAMOFF, &type);
      type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
      ioctl(enc_fd_, VIDIOC_STREAMOFF, &type);
    }
    ESP_LOGI(H264_TAG, "Streaming stopped");
  }

private:
  void printDeviceInfo(int fd, const char *name)
  {
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
    {
      ESP_LOGI(H264_TAG, "%s info:", name);
      ESP_LOGI(H264_TAG, "  Driver: %s", cap.driver);
      ESP_LOGI(H264_TAG, "  Card: %s", cap.card);
      ESP_LOGI(H264_TAG, "  Version: %d.%d.%d",
               (cap.version >> 16) & 0xFF,
               (cap.version >> 8) & 0xFF,
               cap.version & 0xFF);
    }
  }

  bool configureEncoder()
  {
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[4];

    // Set I-frame period
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[0];
    control[0].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
    control[0].value = config_.i_period;
    if (ioctl(enc_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(H264_TAG, "Failed to set I-frame period");
    }

    // Set bitrate
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[1];
    control[1].id = V4L2_CID_MPEG_VIDEO_BITRATE;
    control[1].value = config_.bitrate;
    if (ioctl(enc_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(H264_TAG, "Failed to set bitrate");
    }

    // Set min QP
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[2];
    control[2].id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
    control[2].value = config_.min_qp;
    if (ioctl(enc_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(H264_TAG, "Failed to set min QP");
    }

    // Set max QP
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[3];
    control[3].id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
    control[3].value = config_.max_qp;
    if (ioctl(enc_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(H264_TAG, "Failed to set max QP");
    }

    if (config_.verbose)
    {
      ESP_LOGI(H264_TAG, "Encoder configured: bitrate=%d, I-period=%d, QP=[%d,%d]",
               config_.bitrate, config_.i_period, config_.min_qp, config_.max_qp);
    }
    return true;
  }

  bool setupCapture()
  {
    // Find supported capture format
    // uint32_t capture_fmt = findSupportedCaptureFormat();
    uint32_t capture_fmt = V4L2_PIX_FMT_YUV420;
    if (capture_fmt == 0)
    {
      ESP_LOGE(H264_TAG, "No supported capture format found");
      return false;
    }

    // Set format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = capture_fmt;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(cap_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to set capture format: %s", strerror(errno));
      return false;
    }

    // Get actual format
    if (ioctl(cap_fd_, VIDIOC_G_FMT, &fmt) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to get capture format: %s", strerror(errno));
      return false;
    }
    config_.width = fmt.fmt.pix.width;
    config_.height = fmt.fmt.pix.height;

    char fourcc[5] = {0};
    memcpy(fourcc, &fmt.fmt.pix.pixelformat, 4);
    ESP_LOGI(H264_TAG, "Capture format: %s %dx%d", fourcc, config_.width, config_.height);

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cap_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to request capture buffers: %s", strerror(errno));
      return false;
    }

    // Map buffers
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
      struct v4l2_buffer buf = {};
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;

      if (ioctl(cap_fd_, VIDIOC_QUERYBUF, &buf) < 0)
      {
        ESP_LOGE(H264_TAG, "Failed to query capture buffer %d: %s", i, strerror(errno));
        return false;
      }

      cap_buffer_[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, cap_fd_, buf.m.offset);
      if (cap_buffer_[i] == MAP_FAILED)
      {
        ESP_LOGE(H264_TAG, "Failed to mmap capture buffer %d: %s", i, strerror(errno));
        return false;
      }

      if (ioctl(cap_fd_, VIDIOC_QBUF, &buf) < 0)
      {
        ESP_LOGE(H264_TAG, "Failed to queue capture buffer %d: %s", i, strerror(errno));
        return false;
      }
    }

    // Start capture streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cap_fd_, VIDIOC_STREAMON, &type) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to start capture streaming: %s", strerror(errno));
      return false;
    }

    return true;
  }

  uint32_t findSupportedCaptureFormat()
  {
    // Prefer YUV420 for H264 encoder input
    const uint32_t preferred_formats[] = {
        V4L2_PIX_FMT_YUV420,
        V4L2_PIX_FMT_UYVY,
        V4L2_PIX_FMT_YUYV,
        V4L2_PIX_FMT_RGB565,
        V4L2_PIX_FMT_RGB24};

    for (uint32_t fmt : preferred_formats)
    {
      struct v4l2_format format = {};
      format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      format.fmt.pix.width = config_.width;
      format.fmt.pix.height = config_.height;
      format.fmt.pix.pixelformat = fmt;

      if (ioctl(cap_fd_, VIDIOC_TRY_FMT, &format) == 0 &&
          format.fmt.pix.pixelformat == fmt)
      {
        return fmt;
      }
    }
    return 0;
  }

  bool setupEncoderOutput()
  {
    // Get capture format info
    struct v4l2_format cap_fmt = {};
    cap_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cap_fd_, VIDIOC_G_FMT, &cap_fmt) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to get capture format for encoder");
      return false;
    }

    // Set encoder output format (same as capture format)
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = cap_fmt.fmt.pix.pixelformat;

    if (ioctl(enc_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to set encoder output format: %s", strerror(errno));
      return false;
    }

    // Request buffers for encoder output
    struct v4l2_requestbuffers req = {};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;

    if (ioctl(enc_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to request encoder output buffers: %s", strerror(errno));
      return false;
    }

    return true;
  }

  bool setupEncoderCapture()
  {
    // Set encoder capture format (H264)
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;

    if (ioctl(enc_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to set encoder capture format: %s", strerror(errno));
      return false;
    }

    // Request buffers for encoder capture
    struct v4l2_requestbuffers req = {};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(enc_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to request encoder capture buffers: %s", strerror(errno));
      return false;
    }

    // Query and map buffer
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;

    if (ioctl(enc_fd_, VIDIOC_QUERYBUF, &buf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to query encoder capture buffer: %s", strerror(errno));
      return false;
    }

    enc_buffer_ = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, enc_fd_, buf.m.offset);
    if (enc_buffer_ == MAP_FAILED)
    {
      ESP_LOGE(H264_TAG, "Failed to mmap encoder capture buffer: %s", strerror(errno));
      return false;
    }

    if (ioctl(enc_fd_, VIDIOC_QBUF, &buf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to queue encoder capture buffer: %s", strerror(errno));
      return false;
    }

    return true;
  }

  bool startStreaming()
  {
    // Start encoder capture streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(enc_fd_, VIDIOC_STREAMON, &type) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to start encoder capture stream: %s", strerror(errno));
      return false;
    }

    // Start encoder output streaming
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(enc_fd_, VIDIOC_STREAMON, &type) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to start encoder output stream: %s", strerror(errno));
      return false;
    }

    return true;
  }

  bool dequeueBuffer(int fd, struct v4l2_buffer *buf, int timeout_ms)
  {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &fds, NULL, NULL, &tv);
    if (ret < 0)
    {
      ESP_LOGE(H264_TAG, "select error: %s", strerror(errno));
      return false;
    }
    if (ret == 0)
    {
      ESP_LOGE(H264_TAG, "Timeout waiting for frame");
      return false;
    }

    if (ioctl(fd, VIDIOC_DQBUF, buf) < 0)
    {
      ESP_LOGE(H264_TAG, "Failed to dequeue buffer: %s", strerror(errno));
      return false;
    }

    return true;
  }

  void cleanup()
  {
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
      if (cap_buffer_[i] && cap_buffer_[i] != MAP_FAILED)
      {
        munmap(cap_buffer_[i], 0);
        cap_buffer_[i] = nullptr;
      }
    }

    if (enc_buffer_ && enc_buffer_ != MAP_FAILED)
    {
      munmap(enc_buffer_, 0);
      enc_buffer_ = nullptr;
    }

    if (cap_fd_ >= 0)
    {
      close(cap_fd_);
      cap_fd_ = -1;
    }

    if (enc_fd_ >= 0)
    {
      close(enc_fd_);
      enc_fd_ = -1;
    }
  }

  Config config_;
  int cap_fd_;
  int enc_fd_;
  uint8_t *cap_buffer_[BUFFER_COUNT];
  uint8_t *enc_buffer_;
};

#endif // V4L2_H264_SIMPLE_HPP
