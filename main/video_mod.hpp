/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

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
// #define FRAME_TIMEOUT_MS 17 // ~60 FPS
#define FRAME_TIMEOUT_MS 1000 // 1 second

class V4L2H264Capture
{
public:
  struct Config
  {
    const char *capture_device = "/dev/video0";
    int width = 1920;
    int height = 1080;
    int bitrate = 4000000; // 4 Mbps
    int i_period = 30;     // I-frame every 30 frames
    int min_qp = 10;
    int max_qp = 40;
    bool verbose = true;
  };

  explicit V4L2H264Capture(const Config &config)
      : config_(config), capture_fd_(-1), encoding_fd_(-1), is_initialized_(false)
  {
    cap_buffer_[0] = cap_buffer_[1] = nullptr;
    enc_buffer_ = nullptr;
  }

  ~V4L2H264Capture()
  {
    stop();
    cleanup();
  }

  // Disable copy
  V4L2H264Capture(const V4L2H264Capture &) = delete;
  V4L2H264Capture &operator=(const V4L2H264Capture &) = delete;

  esp_err_t init()
  {
    if (is_initialized_)
    {
      ESP_LOGW(TAG, "Already initialized");
      return ESP_OK;
    }

    // Open capture device
    capture_fd_ = open(config_.capture_device, O_RDWR);
    if (capture_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open capture device: %s", config_.capture_device);
      return ESP_FAIL;
    }

    if (config_.verbose)
    {
      printDeviceInfo(capture_fd_, "Capture Device");
    }

    // Open encoder device
    encoding_fd_ = open(H264_DEVICE_PATH, O_RDWR);
    if (encoding_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open encoder device: %s", H264_DEVICE_PATH);
      return ESP_FAIL;
    }

    if (config_.verbose)
    {
      printDeviceInfo(encoding_fd_, "Encoder Device");
    }

    // Configure encoder
    if (!configureEncoder())
    {
      ESP_LOGE(TAG, "Failed to configure encoder");
      return ESP_FAIL;
    }

    is_initialized_ = true;
    return ESP_OK;
  }

  esp_err_t start()
  {
    if (!is_initialized_)
    {
      ESP_LOGE(TAG, "Not initialized. Call init() first");
      return ESP_ERR_INVALID_STATE;
    }

    if (is_streaming_)
    {
      ESP_LOGW(TAG, "Already streaming");
      return ESP_OK;
    }

    if (!setupCapture())
    {
      ESP_LOGE(TAG, "Failed to setup capture");
      return ESP_FAIL;
    }

    if (!setupEncoderOutput())
    {
      ESP_LOGE(TAG, "Failed to setup encoder output");
      return ESP_FAIL;
    }

    if (!setupEncoderCapture())
    {
      ESP_LOGE(TAG, "Failed to setup encoder capture");
      return ESP_FAIL;
    }

    if (!startStreaming())
    {
      ESP_LOGE(TAG, "Failed to start streaming");
      return ESP_FAIL;
    }

    is_streaming_ = true;
    ESP_LOGI(TAG, "H264 capture started: %dx%d @ %d bps",
             config_.width, config_.height, config_.bitrate);
    return ESP_OK;
  }

  void stop()
  {
    if (!is_streaming_)
    {
      return;
    }

    if (capture_fd_ >= 0)
    {
      int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(capture_fd_, VIDIOC_STREAMOFF, &type);
    }

    if (encoding_fd_ >= 0)
    {
      int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
      type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
      ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
    }

    is_streaming_ = false;
    ESP_LOGI(TAG, "Streaming stopped");
  }

  bool captureFrame(uint8_t *&data, size_t &size, uint32_t &sequence)
  {
    if (capture_fd_ < 0 || encoding_fd_ < 0)
    {
      ESP_LOGE(TAG, "Devices not initialized");
      return false;
    }

    if (!is_streaming_)
    {
      return false;
    }

    struct v4l2_buffer cap_buf = {};
    cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cap_buf.memory = V4L2_MEMORY_MMAP;

    // Get frame from camera
    if (!dequeueBuffer(capture_fd_, &cap_buf, FRAME_TIMEOUT_MS))
    {
      return false;
    }

    // Send to encoder
    struct v4l2_buffer enc_out_buf = {};
    enc_out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_buf.memory = V4L2_MEMORY_USERPTR;
    enc_out_buf.m.userptr = (unsigned long)cap_buffer_[cap_buf.index];
    enc_out_buf.length = cap_buf.bytesused;

    if (ioctl(encoding_fd_, VIDIOC_QBUF, &enc_out_buf) < 0)
    {
      ESP_LOGE(TAG, "Failed to queue buffer to encoder: %s", strerror(errno));
      return false;
    }

    // Get encoded frame
    struct v4l2_buffer enc_cap_buf = {};
    enc_cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(encoding_fd_, VIDIOC_DQBUF, &enc_cap_buf) < 0)
    {
      ESP_LOGE(TAG, "Failed to dequeue encoded frame: %s", strerror(errno));
      return false;
    }

    // Send camera buffer back to queue
    if (ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf) < 0)
    {
      ESP_LOGE(TAG, "Failed to requeue camera buffer: %s", strerror(errno));
    }

    // Dequeue the output buffer from encoder
    struct v4l2_buffer enc_out_debuf = {};
    enc_out_debuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_debuf.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(encoding_fd_, VIDIOC_DQBUF, &enc_out_debuf) < 0)
    {
      ESP_LOGE(TAG, "Failed to dequeue encoder output buffer: %s", strerror(errno));
    }

    // Return frame data
    data = enc_buffer_;
    size = enc_cap_buf.bytesused;
    sequence = enc_cap_buf.sequence;

    // Requeue encoder capture buffer
    struct v4l2_buffer enc_cap_qbuf = {};
    enc_cap_qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_qbuf.memory = V4L2_MEMORY_MMAP;
    enc_cap_qbuf.index = 0;
    
    if (ioctl(encoding_fd_, VIDIOC_QBUF, &enc_cap_qbuf) < 0)
    {
      ESP_LOGE(TAG, "Failed to requeue encoder capture buffer: %s", strerror(errno));
    }

    if (config_.verbose)
    {
      ESP_LOGI(TAG, "Frame %u: %zu bytes", sequence, size);
    }

    return true;
  }

private:
  void printDeviceInfo(int fd, const char *name)
  {
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
    {
      ESP_LOGI(TAG, "%s info:", name);
      ESP_LOGI(TAG, "  Driver: %s", cap.driver);
      ESP_LOGI(TAG, "  Card: %s", cap.card);
      ESP_LOGI(TAG, "  Version: %d.%d.%d",
               (cap.version >> 16) & 0xFF,
               (cap.version >> 8) & 0xFF,
               cap.version & 0xFF);
    }
  }

  bool configureEncoder()
  {
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[4];

    // I-frame period
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[0];
    control[0].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
    control[0].value = config_.i_period;
    if (ioctl(encoding_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(TAG, "Failed to set I-frame period");
    }

    // Set bitrate
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[1];
    control[1].id = V4L2_CID_MPEG_VIDEO_BITRATE;
    control[1].value = config_.bitrate;
    if (ioctl(encoding_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(TAG, "Failed to set bitrate");
    }

    // Set min QP
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[2];
    control[2].id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
    control[2].value = config_.min_qp;
    if (ioctl(encoding_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(TAG, "Failed to set min QP");
    }

    // Set max QP
    controls.ctrl_class = V4L2_CTRL_CLASS_CODEC;
    controls.count = 1;
    controls.controls = &control[3];
    control[3].id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
    control[3].value = config_.max_qp;
    if (ioctl(encoding_fd_, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(TAG, "Failed to set max QP");
    }

    if (config_.verbose)
    {
      ESP_LOGI(TAG, "Encoder configured: bitrate=%d, I-period=%d, QP=[%d,%d]",
               config_.bitrate, config_.i_period, config_.min_qp, config_.max_qp);
    }

    return true;
  }

  bool setupCapture()
  {
    if (capture_fd_ < 0 || encoding_fd_ < 0)
    {
      ESP_LOGE(TAG, "Invalid file descriptors - capture_fd: %d, encoding_fd: %d",
               capture_fd_, encoding_fd_);
      return false;
    }

    // Set capture format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(capture_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(TAG, "Failed to set capture format: %s", strerror(errno));
      return false;
    }

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(capture_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(TAG, "Failed to request capture buffers: %s", strerror(errno));
      return false;
    }

    // Map buffers
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
      struct v4l2_buffer buf = {};
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;

      if (ioctl(capture_fd_, VIDIOC_QUERYBUF, &buf) < 0)
      {
        ESP_LOGE(TAG, "Failed to query capture buffer");
        return false;
      }

      cap_buffer_[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, capture_fd_, buf.m.offset);
      if (cap_buffer_[i] == MAP_FAILED)
      {
        ESP_LOGE(TAG, "Failed to mmap capture buffer");
        return false;
      }

      ioctl(capture_fd_, VIDIOC_QBUF, &buf);
    }

    // Start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(capture_fd_, VIDIOC_STREAMON, &type) < 0)
    {
      ESP_LOGE(TAG, "Failed to start capture streaming");
      return false;
    }

    return true;
  }

  bool setupEncoderOutput()
  {
    // Get capture format
    struct v4l2_format cap_fmt = {};
    cap_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(capture_fd_, VIDIOC_G_FMT, &cap_fmt);

    // Set encoder output format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = cap_fmt.fmt.pix.pixelformat;

    if (ioctl(encoding_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(TAG, "Failed to set encoder output format");
      return false;
    }

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;

    if (ioctl(encoding_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(TAG, "Failed to request encoder output buffers");
      return false;
    }

    return true;
  }

  bool setupEncoderCapture()
  {
    // Set encoder capture format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;

    if (ioctl(encoding_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(TAG, "Failed to set encoder capture format");
      return false;
    }

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(encoding_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(TAG, "Failed to request encoder capture buffers");
      return false;
    }

    // Map buffer
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;

    if (ioctl(encoding_fd_, VIDIOC_QUERYBUF, &buf) < 0)
    {
      ESP_LOGE(TAG, "Failed to query encoder capture buffer");
      return false;
    }

    enc_buffer_ = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, encoding_fd_, buf.m.offset);
    if (enc_buffer_ == MAP_FAILED)
    {
      ESP_LOGE(TAG, "Failed to mmap encoder capture buffer");
      return false;
    }

    ioctl(encoding_fd_, VIDIOC_QBUF, &buf);
    return true;
  }

  bool startStreaming()
  {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(encoding_fd_, VIDIOC_STREAMON, &type);

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(encoding_fd_, VIDIOC_STREAMON, &type);

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
    if (ret <= 0)
    {
      return false;
    }

    if (ioctl(fd, VIDIOC_DQBUF, buf) < 0)
    {
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

    if (capture_fd_ >= 0)
    {
      close(capture_fd_);
      capture_fd_ = -1;
    }

    if (encoding_fd_ >= 0)
    {
      close(encoding_fd_);
      encoding_fd_ = -1;
    }
  }

  Config config_;
  int capture_fd_;
  int encoding_fd_;
  uint8_t *cap_buffer_[BUFFER_COUNT];
  uint8_t *enc_buffer_;
  bool is_initialized_;
  bool is_streaming_ = false;
  static const char *TAG;
};

const char *V4L2H264Capture::TAG = "V4L2_H264";
