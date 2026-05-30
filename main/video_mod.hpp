/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "esp_log.h"
#include "esp_err.h"

#define H264_DEVICE_PATH "/dev/video11"
#define BUFFER_COUNT 2
#define ENCODER_BUFFER_COUNT 3 // Allow pipelining
#define FRAME_TIMEOUT_MS 2     // Reduced from 5ms for lower latency

class V4L2H264Capture
{
public:
  struct Config
  {
    const char *capture_device = "/dev/video0";
    int bitrate = 2000000;
    int i_period = 30;
    int quality = 30;
  };

  explicit V4L2H264Capture(const Config &config)
      : config_(config), capture_fd_(-1), encoding_fd_(-1), is_initialized_(false)
  {
    cap_buffer_[0] = cap_buffer_[1] = nullptr;
    encoder_capture_buffers_.clear();
  }

  esp_err_t init()
  {
    if (is_initialized_)
      return ESP_OK;

    capture_fd_ = open(config_.capture_device, O_RDWR | O_NONBLOCK);
    if (capture_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open capture device: %s", config_.capture_device);
      return ESP_FAIL;
    }

    encoding_fd_ = open(H264_DEVICE_PATH, O_RDWR | O_NONBLOCK);
    if (encoding_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open encoder device: %s", H264_DEVICE_PATH);
      close(capture_fd_);
      return ESP_FAIL;
    }

    find_resolution();

    if (!configureEncoder())
    {
      ESP_LOGE(TAG, "Failed to configure encoder");
      close(capture_fd_);
      close(encoding_fd_);
      return ESP_FAIL;
    }

    is_initialized_ = true;
    return ESP_OK;
  }

  ~V4L2H264Capture()
  {
    if (is_initialized_)
    {
      // Stop streaming
      int type;
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
      type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
      ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(capture_fd_, VIDIOC_STREAMOFF, &type);

      // Unmap encoder capture buffers
      for (auto buf : encoder_capture_buffers_)
        munmap(buf, getEncoderBufferSize());

      // Unmap capture buffers
      for (int i = 0; i < BUFFER_COUNT; i++)
      {
        if (cap_buffer_[i])
          munmap(cap_buffer_[i], getCaptureBufferSize());
      }

      close(capture_fd_);
      close(encoding_fd_);
    }
  }

  esp_err_t start()
  {
    if (!is_initialized_)
    {
      ESP_LOGE(TAG, "Not initialized");
      return ESP_ERR_INVALID_STATE;
    }

    if (!setupCapture())
      return ESP_FAIL;
    if (!setupEncoderOutput())
      return ESP_FAIL;
    if (!setupEncoderCapture())
      return ESP_FAIL;
    if (!startStreaming())
      return ESP_FAIL;

    ESP_LOGI(TAG, "H264 capture started: %dx%d @ %d bps, GOP=%d (pipelined with %d encoder buffers)",
             width_, height_, config_.bitrate, config_.i_period, ENCODER_BUFFER_COUNT);

    return ESP_OK;
  }

  bool captureFrame(uint8_t *&data, size_t &size, uint32_t &sequence)
  {
    std::lock_guard<std::mutex> lock(encoder_mutex_);

    // Step 1: Get raw frame from camera
    struct v4l2_buffer cap_buf = {};
    cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cap_buf.memory = V4L2_MEMORY_MMAP;

    if (!dequeueBufferNonBlocking(capture_fd_, &cap_buf, FRAME_TIMEOUT_MS))
      return false;

    // Step 2: Immediately queue to encoder (non-blocking)
    struct v4l2_buffer enc_out_buf = {};
    enc_out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_buf.memory = V4L2_MEMORY_USERPTR;
    enc_out_buf.m.userptr = (unsigned long)cap_buffer_[cap_buf.index];
    enc_out_buf.length = cap_buf.bytesused;

    if (ioctl(encoding_fd_, VIDIOC_QBUF, &enc_out_buf) < 0)
    {
      ESP_LOGE(TAG, "Failed to queue buffer to encoder: %s", strerror(errno));
      ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf);
      return false;
    }

    // Step 3: Try to get encoded frame (non-blocking)
    struct v4l2_buffer enc_cap_buf = {};
    enc_cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(encoding_fd_, VIDIOC_DQBUF, &enc_cap_buf) < 0)
    {
      if (errno == EAGAIN)
      {
        // Encoder hasn't finished yet, but camera frame was queued
        // Requeue camera buffer for next capture
        ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf);
        return false;
      }
      ESP_LOGE(TAG, "Failed to dequeue encoded frame: %s", strerror(errno));
      ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf);
      return false;
    }

    // Step 4: Clean up - requeue camera buffer
    ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf);

    // Step 5: Dequeue the encoder output buffer (the input we sent)
    struct v4l2_buffer enc_out_debuf = {};
    enc_out_debuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_debuf.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(encoding_fd_, VIDIOC_DQBUF, &enc_out_debuf) < 0)
      ESP_LOGW(TAG, "Failed to dequeue encoder output buffer: %s", strerror(errno));

    // Step 6: Prepare output data
    data = encoder_capture_buffers_[enc_cap_buf.index];
    size = enc_cap_buf.bytesused;
    sequence = enc_cap_buf.sequence;

    // Step 7: Requeue encoder capture buffer
    struct v4l2_buffer enc_cap_qbuf = {};
    enc_cap_qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_qbuf.memory = V4L2_MEMORY_MMAP;
    enc_cap_qbuf.index = enc_cap_buf.index;
    if (ioctl(encoding_fd_, VIDIOC_QBUF, &enc_cap_qbuf) < 0)
      ESP_LOGE(TAG, "Failed to requeue encoder capture buffer: %s", strerror(errno));

    static int frame_count = 0;
    if (++frame_count % 30 == 0)
    {
      static auto last_time = std::chrono::steady_clock::now();
      auto now = std::chrono::steady_clock::now();
      float fps = 30.0f / std::chrono::duration<float>(now - last_time).count();
      last_time = now;
      ESP_LOGI(TAG, "Frame %u: %zu bytes (%.1f KB) - %.1f FPS",
               sequence, size, size / 1024.0, fps);
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
      width_ = 640;
      height_ = 480;
      return;
    }

    width_ = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;
    ESP_LOGI(TAG, "Found resolution: %dx%d", width_, height_);
  }

  size_t getCaptureBufferSize()
  {
    // YUV420: 1.5 bytes per pixel
    return width_ * height_ * 3 / 2;
  }

  size_t getEncoderBufferSize()
  {
    // H264 compressed buffer - usually 1-2x uncompressed size to be safe
    return width_ * height_ * 2;
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
      ESP_LOGW(TAG, "Failed to set control %s (value=%d): %s", param, value, strerror(errno));
      return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Set %s = %d", param, value);
    return ESP_OK;
  }

  bool configureEncoder()
  {
    // Video Bitrate 0x009909cf (int)                : min=25000 max=25000000 step=25000 default=10000000
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_BITRATE, config_.bitrate, "BITRATE");
    // H264 I-Frame Period 0x00990a66 (int)          : min=1 max=120 step=1 default=30
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, config_.i_period, "H264_I_PERIOD");

    // H264 Minimum QP Value 0x00990a61 (int)        : min=0 max=51 step=1 default=25
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, config_.quality, "H264_MIN_QP");
    set_control(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, config_.quality + 5, "H264_MAX_QP");

    // Exposure 0x00980911 (int)        : min=2 max=235 step=1 default=80
    set_control(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_EXPOSURE, 90, "EXPOSURE");
    set_control(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_VFLIP, 1, "VFLIP");
    set_control(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_HFLIP, 0, "HFLIP");

    return true;
  }

  bool setupCapture()
  {
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(capture_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(TAG, "Failed to set capture format: %s", strerror(errno));
      return false;
    }

    struct v4l2_requestbuffers req = {};
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(capture_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(TAG, "Failed to request capture buffers: %s", strerror(errno));
      return false;
    }

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

      if (ioctl(capture_fd_, VIDIOC_QBUF, &buf) < 0)
      {
        ESP_LOGE(TAG, "Failed to queue capture buffer");
        return false;
      }
    }

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
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;

    if (ioctl(encoding_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(TAG, "Failed to set encoder output format");
      return false;
    }

    struct v4l2_requestbuffers req = {};
    req.count = ENCODER_BUFFER_COUNT; // Multiple output buffers for pipelining
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
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;

    if (ioctl(encoding_fd_, VIDIOC_S_FMT, &fmt) < 0)
    {
      ESP_LOGE(TAG, "Failed to set encoder capture format");
      return false;
    }

    struct v4l2_requestbuffers req = {};
    req.count = ENCODER_BUFFER_COUNT; // Multiple capture buffers for pipelining
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(encoding_fd_, VIDIOC_REQBUFS, &req) < 0)
    {
      ESP_LOGE(TAG, "Failed to request encoder capture buffers");
      return false;
    }

    encoder_capture_buffers_.resize(req.count);
    for (unsigned int i = 0; i < req.count; i++)
    {
      struct v4l2_buffer buf = {};
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;

      if (ioctl(encoding_fd_, VIDIOC_QUERYBUF, &buf) < 0)
      {
        ESP_LOGE(TAG, "Failed to query encoder capture buffer");
        return false;
      }

      encoder_capture_buffers_[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                                    MAP_SHARED, encoding_fd_, buf.m.offset);
      if (encoder_capture_buffers_[i] == MAP_FAILED)
      {
        ESP_LOGE(TAG, "Failed to mmap encoder capture buffer");
        return false;
      }

      if (ioctl(encoding_fd_, VIDIOC_QBUF, &buf) < 0)
      {
        ESP_LOGE(TAG, "Failed to queue encoder capture buffer");
        return false;
      }
    }

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

  bool dequeueBufferNonBlocking(int fd, struct v4l2_buffer *buf, int timeout_ms)
  {
    // Try non-blocking dequeue first
    if (ioctl(fd, VIDIOC_DQBUF, buf) >= 0)
      return true;

    if (errno != EAGAIN)
      return false;

    // Wait with select if no frame immediately available
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout_ms * 1000;

    int ret = select(fd + 1, &fds, NULL, NULL, &tv);
    if (ret <= 0)
      return false;

    return ioctl(fd, VIDIOC_DQBUF, buf) >= 0;
  }

  std::mutex encoder_mutex_;
  Config config_;
  int width_ = 640;
  int height_ = 480;
  int capture_fd_ = -1;
  int encoding_fd_ = -1;
  uint8_t *cap_buffer_[BUFFER_COUNT] = {nullptr, nullptr};
  std::vector<uint8_t *> encoder_capture_buffers_;
  bool is_initialized_ = false;
  static const char *TAG;
};

const char *V4L2H264Capture::TAG = "V4L2_H264";
