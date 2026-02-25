#pragma once

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <mutex>
#include <sys/socket.h>
#include "logger.hpp"
#include "esp_system.h"
#include "esp_http_server.h"

#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_video_enc_default.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_capture_advance.h"

#define CAMERA_DEVICE_NAME "/dev/video0"
#define STREAM_TASK_STACK_SIZE (8 * 1024) // 8KB
#define STREAM_TASK_PRIORITY 12
#define STREAM_TASK_CORE_ID 1

#define VENC_TASK_STACK_SIZE (16 * 1024) // 16KB
#define VENC_TASK_PRIORITY 14            // must be higher than STREAM_TASK_PRIORITY to avoid starvation
#define VENC_TASK_CORE_ID 1

namespace ctrl
{
  class VideoStream
  {
  public:
    explicit VideoStream()
        : logger_({.tag = "VideoStream", .level = espp::Logger::Verbosity::INFO})
    {
      instance_ = this;
    }

    ~VideoStream()
    {
      cleanup();
    }

    esp_err_t init()
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);

      esp_video_enc_register_default();

      esp_capture_set_thread_scheduler(thread_scheduler);

      memset(&capture_sys_, 0, sizeof(capture_sys_));

      esp_err_t ret = build_capture(&capture_sys_);

      if (ret != ESP_OK)
      {
        logger_.error("Failed to build video capture: {}", ret);
        return ret;
      }

      esp_capture_sink_cfg_t sink_cfg = {
          .video_info = {
              .format_id = ESP_CAPTURE_FMT_ID_H264,
              .width = CONFIG_VIDEO_STREAM_WIDTH,
              .height = CONFIG_VIDEO_STREAM_HEIGHT,
              .fps = CONFIG_VIDEO_STREAM_FPS,
          },
      };

      ret = esp_capture_sink_setup(capture_sys_.capture, 0, &sink_cfg, &stream_sink_);
      if (ret != ESP_CAPTURE_ERR_OK)
      {
        logger_.error("Failed to setup sink: {}", ret);
        destroy_capture(&capture_sys_);
        return ret;
      }

      esp_capture_sink_enable(stream_sink_, ESP_CAPTURE_RUN_MODE_ALWAYS);

      ret = esp_capture_start(capture_sys_.capture);
      if (ret != ESP_CAPTURE_ERR_OK)
      {
        logger_.error("Failed to start capture: {}", ret);
        destroy_capture(&capture_sys_);
        return ret;
      }

      logger_.info("Video capture initialized successfully");
      return ESP_OK;
    }

    void cleanup()
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);

      esp_capture_stop(capture_sys_.capture);
      destroy_capture(&capture_sys_);
      stream_sink_ = nullptr;

      logger_.info("Video capture cleaned up");
    }

    static esp_err_t handle_stream(httpd_req_t *req)
    {
      instance_->logger_.info("Client connected");
      int sock = httpd_req_to_sockfd(req);

      // Send HTTP headers directly on the raw socket before handing it off
      const char *headers =
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: video/h264\r\n"
          "Cache-Control: no-cache, no-store, must-revalidate\r\n"
          "Pragma: no-cache\r\n"
          "Expires: 0\r\n"
          "Access-Control-Allow-Origin: *\r\n"
          "Accept-Ranges: none\r\n"
          "Transfer-Encoding: chunked\r\n"
          "Connection: close\r\n"
          "\r\n";
      send(sock, headers, strlen(headers), 0);

      // Allocate context on the heap — task owns it and must free it
      auto *ctx = new StreamTaskCtx{
          .sink = instance_->stream_sink_,
          .sock = sock,
      };

      xTaskCreatePinnedToCore(
          stream_task,
          "stream_task",
          STREAM_TASK_STACK_SIZE,
          ctx,
          STREAM_TASK_PRIORITY,
          nullptr,
          STREAM_TASK_CORE_ID);

      // Return ESP_OK without calling httpd_resp_send — httpd will not close the socket
      return ESP_OK;
    }

  private:
    typedef struct
    {
      esp_capture_handle_t capture;
      esp_capture_video_src_if_t *vid_src;
    } capture_sys_t;

    espp::Logger logger_;
    capture_sys_t capture_sys_{};
    esp_capture_sink_handle_t stream_sink_{nullptr};
    std::mutex capture_mutex_;
    static VideoStream *instance_;

    struct StreamTaskCtx
    {
      esp_capture_sink_handle_t sink;
      int sock;
    };

    static void stream_task(void *arg)
    {
      instance_->logger_.info("Stream task");
      auto *ctx = static_cast<StreamTaskCtx *>(arg);

      int frame_count = 0;
      int dropped_count = 0;
      esp_capture_stream_frame_t frame = {};
      frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;

      int fps_frame_count = 0;
      int64_t fps_last_time = esp_timer_get_time(); // microseconds

      while (true)
      {
        esp_err_t err = esp_capture_sink_acquire_frame(ctx->sink, &frame, false);
        if (err != ESP_CAPTURE_ERR_OK)
        {
          dropped_count++;
          esp_capture_sink_release_frame(ctx->sink, &frame);
          continue;
        }

        dropped_count = 0;

        // Format as HTTP chunked encoding
        char chunk_header[16];
        int header_len = snprintf(chunk_header, sizeof(chunk_header), "%x\r\n", frame.size);

        bool disconnected =
            send(ctx->sock, chunk_header, header_len, 0) < 0 ||
            send(ctx->sock, (const char *)frame.data, frame.size, 0) < 0 ||
            send(ctx->sock, "\r\n", 2, 0) < 0;

        esp_capture_sink_release_frame(ctx->sink, &frame);
        vTaskDelay(pdMS_TO_TICKS(1)); // yield after every frame — keeps IDLE scheduled

        if (disconnected)
        {
          instance_->logger_.info("Client disconnected, frames={}, dropped={}", frame_count, dropped_count);
          break;
        }

        frame_count++;
        fps_frame_count++;

        // Log FPS every 3 seconds
        int64_t now = esp_timer_get_time();
        int64_t elapsed_us = now - fps_last_time;
        if (elapsed_us >= 3'000'000)
        {
          float fps = fps_frame_count / (elapsed_us / 1'000'000.0f);
          instance_->logger_.info("Streaming FPS: {:.1f}, total frames={}, dropped={}", fps, frame_count, dropped_count);
          fps_frame_count = 0;
          fps_last_time = now;
        }
      }

      // Send chunked EOF
      send(ctx->sock, "0\r\n\r\n", 5, 0);

      close(ctx->sock);
      delete ctx;
      vTaskDelete(nullptr);
    }

    static void thread_scheduler(const char *thread_name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
    {
      schedule_cfg->core_id = VENC_TASK_CORE_ID;
      schedule_cfg->stack_size = VENC_TASK_STACK_SIZE;
      schedule_cfg->priority = VENC_TASK_PRIORITY;
    }

    static void set_camera_controls(const char *dev_name)
    {
      int fd = open(dev_name, O_RDWR);
      if (fd < 0)
      {
        return;
      }

      // Helper lambda/struct to set a control
      auto set_ctrl = [fd](uint32_t id, int32_t value)
      {
        struct v4l2_control ctrl = {
            .id = id,
            .value = value,
        };
        ioctl(fd, VIDIOC_S_CTRL, &ctrl);
      };

      // Increase brightness (typical range: -64 to 64, default 0)
      set_ctrl(V4L2_CID_BRIGHTNESS, 60);

      // Increase exposure (if manual mode is supported)
      set_ctrl(V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL);
      set_ctrl(V4L2_CID_EXPOSURE_ABSOLUTE, 500); // value depends on sensor

      // Increase gain (typical range: 0–255)
      set_ctrl(V4L2_CID_GAIN, 80);

      // Or increase contrast
      set_ctrl(V4L2_CID_CONTRAST, 30);

      close(fd);
    }

    static esp_capture_video_src_if_t *create_video_source()
    {
      esp_capture_video_v4l2_src_cfg_t v4l2_cfg = {
          .dev_name = CAMERA_DEVICE_NAME,
          .buf_count = 8,
      };
      return esp_capture_new_video_v4l2_src(&v4l2_cfg);
    }

    static int build_capture(capture_sys_t *capture_sys)
    {
      capture_sys->vid_src = create_video_source();
      if (capture_sys->vid_src == NULL)
      {
        instance_->logger_.error("Fail to create video source");
        return -1;
      }
      set_camera_controls(CAMERA_DEVICE_NAME);

      esp_capture_cfg_t capture_cfg = {
          .sync_mode = ESP_CAPTURE_SYNC_MODE_SYSTEM,
          .video_src = capture_sys->vid_src,
      };

      esp_capture_open(&capture_cfg, &capture_sys->capture);
      if (capture_sys->capture == NULL)
      {
        instance_->logger_.error("Fail to create capture");
        return -1;
      }

      return 0;
    }

    static void destroy_capture(capture_sys_t *capture_sys)
    {
      if (capture_sys->capture)
      {
        esp_capture_close(capture_sys->capture);
        capture_sys->capture = NULL;
      }
      if (capture_sys->vid_src)
      {
        free(capture_sys->vid_src);
        capture_sys->vid_src = NULL;
      }
    }
  };

  VideoStream *VideoStream::instance_ = nullptr;

}
