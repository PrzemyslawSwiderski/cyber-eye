/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "esp_log.h"
#include "esp_err.h"

#define H264_DEVICE_PATH "/dev/video11"
#define BUFFER_COUNT 5
// #define FRAME_TIMEOUT_MS 17 // ~60 FPS
#define FRAME_TIMEOUT_MS 1

class V4L2H264Capture
{
public:
  struct Config
  {
    const char *capture_device = "/dev/video0";
    int bitrate = 2000000; // 2 Mbps (reduced from 4)
    int i_period = 30;     // I-frame every 30 frames
    int min_qp = 20;       // Increased for smaller frames
    int max_qp = 40;
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

  esp_err_t init()
  {
    if (is_initialized_)
    {
      return ESP_OK;
    }

    // Open capture device
    capture_fd_ = open(config_.capture_device, O_RDWR);
    if (capture_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open capture device: %s", config_.capture_device);
      return ESP_FAIL;
    }

    // Open encoder device
    encoding_fd_ = open(H264_DEVICE_PATH, O_RDWR);
    if (encoding_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open encoder device: %s", H264_DEVICE_PATH);
      return ESP_FAIL;
    }

    find_resolution();

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
      ESP_LOGE(TAG, "Not initialized");
      return ESP_ERR_INVALID_STATE;
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

    ESP_LOGI(TAG, "H264 capture started: %dx%d @ %d bps, GOP=%d",
             width, height, config_.bitrate, config_.i_period);

    return ESP_OK;
  }

  void stop()
  {
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

    ESP_LOGI(TAG, "Streaming stopped");
  }

  bool captureFrame(uint8_t *&data, size_t &size, uint32_t &sequence)
  {
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

    // Dequeue output buffer from encoder
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

    // Log frame info periodically
    static int frame_count = 0;
    if (++frame_count % 30 == 0)
    {
      ESP_LOGI(TAG, "Frame %u: %zu bytes (%.1f KB)", sequence, size, size / 1024.0);
    }

    return true;
  }

private:
  void find_resolution()
  {
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(capture_fd_, VIDIOC_G_FMT, &fmt) < 0)
    {
      ESP_LOGE(TAG, "Failed to find resolution");
    }

    ESP_LOGI(TAG, "Found resolution: %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);
    width = fmt.fmt.pix.width;
    height = fmt.fmt.pix.height;
  }

  esp_err_t set_control(int fd, uint32_t ctrl_class, uint32_t id, int32_t value, const char *param)
  {
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

    controls.ctrl_class = ctrl_class;
    controls.count = 1;
    controls.controls = control;
    control[0].id = id;
    control[0].value = value;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
      ESP_LOGW(TAG, "failed to set control: %s, error: %s", param, strerror(errno));
      return ESP_FAIL;
    }
    ESP_LOGI(TAG, "setting control: %s, to: %d", param, value);
    return ESP_OK;
  }

  bool configureEncoder()
  {

    // H264 I-Frame Period 0x00990a66 (int)          : min=1 max=120 step=1 default=30
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, config_.i_period, "H264_I_PERIOD");
    // Video Bitrate 0x009909cf (int)                : min=25000 max=25000000 step=25000 default=10000000
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_BITRATE, config_.bitrate, "BITRATE");
    // H264 Minimum QP Value 0x00990a61 (int)        : min=0 max=51 step=1 default=25
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, config_.min_qp, "H264_MIN_QP");
    // H264 Maximum QP Value 0x00990a62 (int)        : min=0 max=51 step=1 default=26
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, config_.max_qp, "H264_MAX_QP");

    // Exposure 0x00980911 (int)        : min=2 max=235 step=1 default=80
    set_control(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_EXPOSURE, 90, "V4L2_CID_EXPOSURE");
    // Vertical Flip 0x00980915 (int)   : min=0 max=1 step=1 default=0
    set_control(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_VFLIP, 1, "V4L2_CID_VFLIP");
    // Horizontal Flip 0x00980914 (int) : min=0 max=1 step=1 default=0
    set_control(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_HFLIP, 0, "V4L2_CID_HFLIP");

    return true;
  }

  bool setupCapture()
  {

    // Set capture format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
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

    // Start capture streaming
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
    // Set encoder output format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;

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
    // Set encoder capture format (H264 output)
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
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
    if (ioctl(encoding_fd_, VIDIOC_STREAMON, &type) < 0)
    {
      ESP_LOGE(TAG, "Failed to start encoder capture streaming");
      return false;
    }

    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(encoding_fd_, VIDIOC_STREAMON, &type) < 0)
    {
      ESP_LOGE(TAG, "Failed to start encoder output streaming");
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
  int width;
  int height;
  int capture_fd_;
  int encoding_fd_;
  uint8_t *cap_buffer_[BUFFER_COUNT];
  uint8_t *enc_buffer_;
  bool is_initialized_;
  static const char *TAG;
};

const char *V4L2H264Capture::TAG = "V4L2_H264";
