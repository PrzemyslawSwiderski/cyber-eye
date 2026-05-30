/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: ESPRESSIF MIT
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "esp_log.h"
#include "esp_err.h"

class V4L2H264Capture
{
public:
  struct Config
  {
    const char *capture_device = "/dev/video0";
    int bitrate = 2000000;
    int i_period = 30;
    int quality = 30;
    int exposure = 90;
    int width = 0;
    int height = 0;
  };

  explicit V4L2H264Capture(const Config &config) : config_(config) {}

  ~V4L2H264Capture()
  {
    stop();
    for (int i = 0; i < BUFFER_COUNT; i++)
      if (cap_buffer_[i])
        munmap(cap_buffer_[i], width_ * height_ * 3 / 2);
    for (int i = 0; i < ENCODER_BUFFER_COUNT; i++)
      if (enc_buffers_[i])
        munmap(enc_buffers_[i], enc_buffer_size_);
    if (capture_fd_ >= 0)
      close(capture_fd_);
    if (encoding_fd_ >= 0)
      close(encoding_fd_);
  }

  esp_err_t init()
  {
    if (initialized_)
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
      capture_fd_ = -1;
      return ESP_FAIL;
    }

    if (config_.width > 0 && config_.height > 0)
    {
      width_ = config_.width;
      height_ = config_.height;
      ESP_LOGI(TAG, "Using configured resolution: %dx%d", width_, height_);
    }
    else
    {
      struct v4l2_format fmt;
      memset(&fmt, 0, sizeof(fmt));
      fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (ioctl(capture_fd_, VIDIOC_G_FMT, &fmt) < 0)
      {
        ESP_LOGE(TAG, "Failed to get resolution");
        return ESP_FAIL;
      }
      width_ = fmt.fmt.pix.width;
      height_ = fmt.fmt.pix.height;
      ESP_LOGI(TAG, "Detected resolution: %dx%d", width_, height_);
    }

    if (!configureEncoder())
      return ESP_FAIL;
    initialized_ = true;
    return ESP_OK;
  }

  esp_err_t start()
  {
    if (!initialized_)
      return ESP_ERR_INVALID_STATE;
    if (streaming_)
      return ESP_OK;

    if (!setupCapture())
      return ESP_FAIL;
    if (!setupEncoderOutput())
      return ESP_FAIL;
    if (!setupEncoderCapture())
      return ESP_FAIL;
    if (!startStreaming())
      return ESP_FAIL;

    streaming_ = true;
    frame_sequence_ = 0;
    ESP_LOGI(TAG, "H264 capture started: %dx%d @ %d bps, GOP=%d",
             width_, height_, config_.bitrate, config_.i_period);
    return ESP_OK;
  }

  void stop()
  {
    if (!streaming_)
      return;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(capture_fd_, VIDIOC_STREAMOFF, &type);
    streaming_ = false;
  }

  void updateConfig(const Config &config)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bool was_running = streaming_;
    if (was_running)
      stop();

    config_ = config;
    if (config_.width > 0)
      width_ = config_.width;
    if (config_.height > 0)
      height_ = config_.height;

    if (initialized_ && encoding_fd_ >= 0)
      configureEncoder();
    if (was_running)
      start();
  }

  bool captureFrame(uint8_t *&data, size_t &size, uint32_t &sequence)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!streaming_)
      return false;

    struct v4l2_buffer cap_buf;
    memset(&cap_buf, 0, sizeof(cap_buf));
    cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cap_buf.memory = V4L2_MEMORY_MMAP;

    if (!dequeueBuffer(capture_fd_, &cap_buf, FRAME_TIMEOUT_MS))
      return false;

    struct v4l2_buffer enc_out_buf;
    memset(&enc_out_buf, 0, sizeof(enc_out_buf));
    enc_out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_buf.memory = V4L2_MEMORY_USERPTR;
    enc_out_buf.m.userptr = (unsigned long)cap_buffer_[cap_buf.index];
    enc_out_buf.length = cap_buf.bytesused;

    if (ioctl(encoding_fd_, VIDIOC_QBUF, &enc_out_buf) < 0)
    {
      ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf);
      return false;
    }

    struct v4l2_buffer enc_cap_buf;
    memset(&enc_cap_buf, 0, sizeof(enc_cap_buf));
    enc_cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(encoding_fd_, VIDIOC_DQBUF, &enc_cap_buf) < 0)
    {
      ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf);
      return false;
    }

    ioctl(capture_fd_, VIDIOC_QBUF, &cap_buf);

    struct v4l2_buffer enc_out_debuf;
    memset(&enc_out_debuf, 0, sizeof(enc_out_debuf));
    enc_out_debuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enc_out_debuf.memory = V4L2_MEMORY_USERPTR;
    ioctl(encoding_fd_, VIDIOC_DQBUF, &enc_out_debuf);

    data = enc_buffers_[enc_cap_buf.index];
    size = enc_cap_buf.bytesused;
    sequence = frame_sequence_++;

    struct v4l2_buffer enc_cap_qbuf;
    memset(&enc_cap_qbuf, 0, sizeof(enc_cap_qbuf));
    enc_cap_qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enc_cap_qbuf.memory = V4L2_MEMORY_MMAP;
    enc_cap_qbuf.index = enc_cap_buf.index;
    ioctl(encoding_fd_, VIDIOC_QBUF, &enc_cap_qbuf);

    static int frame_count = 0;
    if (++frame_count % 30 == 0)
    {
      static auto last = std::chrono::steady_clock::now();
      auto now = std::chrono::steady_clock::now();
      float fps = 30.0f / std::chrono::duration<float>(now - last).count();
      last = now;
      ESP_LOGI(TAG, "Frame %u: %zu bytes (%.1f FPS)", sequence, size, fps);
    }

    return true;
  }

  const Config &getConfig() const { return config_; }
  int getWidth() const { return width_; }
  int getHeight() const { return height_; }
  bool isRunning() const { return streaming_; }

private:
  static const char *TAG;
  static constexpr const char *H264_DEVICE_PATH = "/dev/video11";
  static constexpr int BUFFER_COUNT = 2;
  static constexpr int ENCODER_BUFFER_COUNT = 3;
  static constexpr int FRAME_TIMEOUT_MS = 2;

  bool configureEncoder()
  {
    // Video Bitrate 0x009909cf (int)                : min=25000 max=25000000 step=25000 default=10000000
    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_BITRATE, config_.bitrate, "BITRATE");
    // H264 I-Frame Period 0x00990a66 (int)          : min=1 max=120 step=1 default=30
    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, config_.i_period, "H264_I_PERIOD");

    // H264 Minimum QP Value 0x00990a61 (int)        : min=0 max=51 step=1 default=25
    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, config_.quality, "H264_MIN_QP");
    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, config_.quality + 5, "H264_MAX_QP");

    // Exposure 0x00980911 (int)        : min=2 max=235 step=1 default=80
    setControl(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_EXPOSURE, config_.exposure, "EXPOSURE");
    setControl(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_VFLIP, 1, "VFLIP");
    setControl(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_HFLIP, 0, "HFLIP");

    return true;
  }

  void setControl(int fd, uint32_t ctrl_class, uint32_t id, int32_t value, const char *param)
  {
    struct v4l2_ext_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = value;

    struct v4l2_ext_controls ctrls;
    memset(&ctrls, 0, sizeof(ctrls));
    ctrls.ctrl_class = ctrl_class;
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0)
      ESP_LOGW(TAG, "Failed to set %s=%d: %s", param, value, strerror(errno));
    else
      ESP_LOGI(TAG, "Set %s = %d", param, value);
  }

  bool setupCapture()
  {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(capture_fd_, VIDIOC_S_FMT, &fmt) < 0)
      return false;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(capture_fd_, VIDIOC_REQBUFS, &req) < 0)
      return false;

    for (int i = 0; i < BUFFER_COUNT; i++)
    {
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;

      if (ioctl(capture_fd_, VIDIOC_QUERYBUF, &buf) < 0)
        return false;

      cap_buffer_[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, capture_fd_, buf.m.offset);
      if (cap_buffer_[i] == MAP_FAILED)
        return false;
      if (ioctl(capture_fd_, VIDIOC_QBUF, &buf) < 0)
        return false;
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    return ioctl(capture_fd_, VIDIOC_STREAMON, &type) >= 0;
  }

  bool setupEncoderOutput()
  {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;

    if (ioctl(encoding_fd_, VIDIOC_S_FMT, &fmt) < 0)
      return false;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = ENCODER_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;

    return ioctl(encoding_fd_, VIDIOC_REQBUFS, &req) >= 0;
  }

  bool setupEncoderCapture()
  {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;

    if (ioctl(encoding_fd_, VIDIOC_S_FMT, &fmt) < 0)
      return false;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = ENCODER_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(encoding_fd_, VIDIOC_REQBUFS, &req) < 0)
      return false;

    for (unsigned int i = 0; i < ENCODER_BUFFER_COUNT; i++)
    {
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;

      if (ioctl(encoding_fd_, VIDIOC_QUERYBUF, &buf) < 0)
        return false;

      enc_buffer_size_ = buf.length;
      enc_buffers_[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, encoding_fd_, buf.m.offset);
      if (enc_buffers_[i] == MAP_FAILED)
        return false;
      if (ioctl(encoding_fd_, VIDIOC_QBUF, &buf) < 0)
        return false;
    }
    return true;
  }

  bool startStreaming()
  {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(encoding_fd_, VIDIOC_STREAMON, &type) < 0)
      return false;
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    return ioctl(encoding_fd_, VIDIOC_STREAMON, &type) >= 0;
  }

  bool dequeueBuffer(int fd, struct v4l2_buffer *buf, int timeout_ms)
  {
    if (ioctl(fd, VIDIOC_DQBUF, buf) >= 0)
      return true;
    if (errno != EAGAIN)
      return false;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeout_ms * 1000;

    return select(fd + 1, &fds, NULL, NULL, &tv) > 0 && ioctl(fd, VIDIOC_DQBUF, buf) >= 0;
  }

  Config config_;
  int width_ = 640, height_ = 480;
  int capture_fd_ = -1, encoding_fd_ = -1;
  uint8_t *cap_buffer_[2] = {nullptr, nullptr};
  uint8_t *enc_buffers_[ENCODER_BUFFER_COUNT] = {nullptr};
  size_t enc_buffer_size_ = 0;
  uint32_t frame_sequence_ = 0;
  bool initialized_ = false, streaming_ = false;
  std::mutex mutex_;
};

const char *V4L2H264Capture::TAG = "V4L2_H264";
