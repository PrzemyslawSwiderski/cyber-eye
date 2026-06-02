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
    int i_period = 30;
    int quality = 40;
    int exposure = 80;
    int width = 1280;
    int height = 960;
  };

  explicit V4L2H264Capture(const Config &config) : config_(config) {}

  ~V4L2H264Capture()
  {
    ESP_LOGI(TAG, "Destroying capture device");
    stop();
    cleanupResources();
  }

  esp_err_t init()
  {
    if (initialized_)
      return ESP_OK;

    capture_fd_ = open(config_.capture_device, O_RDWR | O_NONBLOCK);
    if (capture_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open capture device");
      return ESP_FAIL;
    }

    encoding_fd_ = open(H264_DEVICE_PATH, O_RDWR | O_NONBLOCK);
    if (encoding_fd_ < 0)
    {
      ESP_LOGE(TAG, "Failed to open encoder device");
      close(capture_fd_);
      capture_fd_ = -1;
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Resolution: %dx%d", config_.width, config_.height);
    configureEncoder();
    initialized_ = true;
    return ESP_OK;
  }

  esp_err_t start()
  {
    if (!initialized_ || streaming_)
      return ESP_ERR_INVALID_STATE;

    return startInternal() ? ESP_OK : ESP_FAIL;
  }

  void stop()
  {
    if (!streaming_)
      return;
    stopInternal();
  }

  void updateConfig(const Config &config)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    stopInternal();

    config_ = config;

    closeEncoder();
    openEncoder();
    configureEncoder();

    startInternal();
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
      struct v4l2_buffer tmp;
      memset(&tmp, 0, sizeof(tmp));
      tmp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
      tmp.memory = V4L2_MEMORY_USERPTR;
      ioctl(encoding_fd_, VIDIOC_DQBUF, &tmp);
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

    return true;
  }

  const Config &getConfig() const { return config_; }

private:
  static const char *TAG;
  static constexpr const char *H264_DEVICE_PATH = "/dev/video11";
  static constexpr int BUFFER_COUNT = 2;
  static constexpr int ENCODER_BUFFER_COUNT = 3;
  static constexpr int FRAME_TIMEOUT_MS = 2;

  Config config_;
  int capture_fd_ = -1, encoding_fd_ = -1;
  uint8_t *cap_buffer_[BUFFER_COUNT] = {nullptr};
  uint8_t *enc_buffers_[ENCODER_BUFFER_COUNT] = {nullptr};
  size_t enc_buffer_size_ = 0;
  uint32_t frame_sequence_ = 0;
  bool initialized_ = false, streaming_ = false;
  std::mutex mutex_;

  void stopInternal()
  {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(capture_fd_, VIDIOC_STREAMOFF, &type);
    cleanupBuffers();
    streaming_ = false;
  }

  bool startInternal()
  {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(capture_fd_, VIDIOC_STREAMOFF, &type);
    cleanupBuffers();

    if (!setupCapture() || !setupEncoderOutput() || !setupEncoderCapture() || !startStreaming())
    {
      ESP_LOGE(TAG, "Failed to start streaming");
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
      type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
      ioctl(encoding_fd_, VIDIOC_STREAMOFF, &type);
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ioctl(capture_fd_, VIDIOC_STREAMOFF, &type);
      cleanupBuffers();
      return false;
    }

    streaming_ = true;
    frame_sequence_ = 0;
    ESP_LOGI(TAG, "Capture started: %dx%d GOP=%d quality=%d", config_.width, config_.height, config_.i_period, config_.quality);
    return true;
  }

  void openEncoder()
  {
    encoding_fd_ = open(H264_DEVICE_PATH, O_RDWR | O_NONBLOCK);
    if (encoding_fd_ < 0)
      ESP_LOGE(TAG, "Failed to open encoder");
  }

  void closeEncoder()
  {
    if (encoding_fd_ >= 0)
    {
      close(encoding_fd_);
      encoding_fd_ = -1;
    }
  }

  void configureEncoder()
  {
    if (encoding_fd_ < 0)
      return;

    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_BITRATE, 25000000, "BITRATE");
    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, config_.i_period, "I_PERIOD");

    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, std::max(1, config_.quality), "MIN_QP");
    setControl(encoding_fd_, V4L2_CID_CODEC_CLASS, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, std::min(51, config_.quality + 5), "MAX_QP");

    setControl(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_EXPOSURE, config_.exposure, "EXPOSURE");
    setControl(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_VFLIP, 1, "VFLIP");
    setControl(capture_fd_, V4L2_CTRL_CLASS_USER, V4L2_CID_HFLIP, 0, "HFLIP");
  }

  void setControl(int fd, uint32_t ctrl_class, uint32_t id, int32_t value, const char *param)
  {
    if (fd < 0)
      return;

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
      ESP_LOGW(TAG, "Failed to set %s=%d", param, value);
    else
      ESP_LOGI(TAG, "Set %s = %d", param, value);
  }

  bool setupCapture()
  {
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
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
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
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
    fmt.fmt.pix.width = config_.width;
    fmt.fmt.pix.height = config_.height;
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

  void cleanupBuffers()
  {
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
      if (cap_buffer_[i])
      {
        munmap(cap_buffer_[i], config_.width * config_.height * 3 / 2);
        cap_buffer_[i] = nullptr;
      }
    }
    for (int i = 0; i < ENCODER_BUFFER_COUNT; i++)
    {
      if (enc_buffers_[i])
      {
        munmap(enc_buffers_[i], enc_buffer_size_);
        enc_buffers_[i] = nullptr;
      }
    }
  }

  void cleanupResources()
  {
    cleanupBuffers();
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
};

const char *V4L2H264Capture::TAG = "V4L2_H264";
