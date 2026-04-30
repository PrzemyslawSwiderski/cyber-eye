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
#define STREAM_TASK_PRIORITY 5
#define STREAM_TASK_CORE_ID 1

#define VENC_TASK_STACK_SIZE (16 * 1024) // 16KB
#define VENC_TASK_PRIORITY 6            // must be higher than STREAM_TASK_PRIORITY to avoid starvation
#define VENC_TASK_CORE_ID 1

namespace ctrl
{
  class VideoStreamWs
  {
  public:
    explicit VideoStreamWs()
        : logger_({.tag = "VideoStreamWs", .level = espp::Logger::Verbosity::INFO})
    {
      instance_ = this;
    }

    ~VideoStreamWs()
    {
      cleanup();
    }

    esp_err_t init(httpd_handle_t http_server)
    {
      // Register WebSocket endpoint
      httpd_uri_t ws_uri = {
          .uri = "/stream.ws",
          .method = HTTP_GET,
          .handler = handle_websocket,
          .user_ctx = this,
          .is_websocket = true,
          .handle_ws_control_frames = true,
          .supported_subprotocol = nullptr};

      esp_err_t ret = httpd_register_uri_handler(http_server, &ws_uri);
      if (ret != ESP_OK)
      {
        logger_.error("Failed to register WebSocket URI handler: {}", ret);
        return ret;
      }

      esp_video_enc_register_default();

      esp_capture_set_thread_scheduler(thread_scheduler);

      memset(&capture_sys_, 0, sizeof(capture_sys_));

      ret = build_capture(&capture_sys_);

      if (ret != ESP_OK)
      {
        logger_.error("Failed to build video capture: {}", ret);
        return ret;
      }

      esp_capture_sink_cfg_t sink_cfg = {
          .audio_info = {},
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
      esp_capture_stop(capture_sys_.capture);
      destroy_capture(&capture_sys_);
      stream_sink_ = nullptr;

      logger_.info("Video capture cleaned up");
    }

    static esp_err_t handle_websocket(httpd_req_t *req)
    {
      instance_->logger_.info("WebSocket client connected");

      if (req->method == HTTP_GET)
      {
        instance_->logger_.info("Handshake done, the new connection was opened");
      }

      // Get the WebSocket handle from the request
      httpd_ws_frame_t ws_frame = {};
      ws_frame.type = HTTPD_WS_TYPE_BINARY;

      // Create context for WebSocket task
      auto *ctx = new WebSocketTaskCtx{
          .sink = instance_->stream_sink_,
          .req = req};

      websocket_stream_task(ctx);
      // xTaskCreatePinnedToCore(
      //     websocket_stream_task,
      //     "websocket_task",
      //     STREAM_TASK_STACK_SIZE,
      //     ctx,
      //     STREAM_TASK_PRIORITY,
      //     nullptr,
      //     STREAM_TASK_CORE_ID);

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
    static VideoStreamWs *instance_;

    struct WebSocketTaskCtx
    {
      esp_capture_sink_handle_t sink;
      httpd_req_t *req;
      int sock; // Raw socket for monitoring
    };

    static void websocket_stream_task(void *arg)
    {
      instance_->logger_.info("WebSocket stream task started");
      auto *ctx = static_cast<WebSocketTaskCtx *>(arg);

      // // Get the WebSocket handle
      // ctx->sock = httpd_req_to_sockfd(ctx->req);

      // // Configure TCP for better streaming
      // int opt = 1;
      // setsockopt(ctx->sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

      // int sndbuf = 64 * 1024; // 64KB - larger buffer for WebSocket
      // setsockopt(ctx->sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

      int frame_count = 0;
      int dropped_count = 0;
      esp_capture_stream_frame_t frame = {};
      frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;

      int fps_frame_count = 0;
      int64_t fps_last_time = esp_timer_get_time();

      while (true)
      {
        // Acquire frame
        esp_err_t err = esp_capture_sink_acquire_frame(ctx->sink, &frame, false);
        if (err != ESP_CAPTURE_ERR_OK)
        {
          dropped_count++;
          continue;
        }

        // Prepare WebSocket binary frame
        httpd_ws_frame_t ws_frame = {
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = (uint8_t *)frame.data,
            .len = (size_t)frame.size};

        // Send via WebSocket API
        esp_err_t send_err = httpd_ws_send_frame(ctx->req, &ws_frame);

        esp_capture_sink_release_frame(ctx->sink, &frame);

        if (send_err != ESP_OK)
        {
          instance_->logger_.info("WebSocket client disconnected, frames={}, dropped={}",
                                  frame_count, dropped_count);
          break;
        }

        frame_count++;
        fps_frame_count++;

        // FPS logging
        int64_t now = esp_timer_get_time();
        int64_t elapsed_us = now - fps_last_time;
        if (elapsed_us >= 3'000'000)
        {
          float fps = fps_frame_count / (elapsed_us / 1'000'000.0f);
          instance_->logger_.info("WebSocket FPS: {:.1f}, total frames={}", fps, frame_count);
          fps_frame_count = 0;
          fps_last_time = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
      }

      // Send WebSocket close frame
      httpd_ws_frame_t close_frame = {
          .type = HTTPD_WS_TYPE_CLOSE,
          .payload = NULL,
          .len = 0};
      httpd_ws_send_frame(ctx->req, &close_frame);

      // close(ctx->sock);
      delete ctx;

      instance_->logger_.info("WebSocket stream task ended");
      vTaskDelete(nullptr);
    }

    static std::string get_frame_type(const uint8_t *data, size_t size)
    {
      // H264 start codes: 0x00 0x00 0x01 or 0x00 0x00 0x00 0x01
      // Byte after start code: lower 5 bits = NAL type
      //   NAL type 5 = IDR slice (I-frame)
      //   NAL type 7 = SPS (often precedes I-frame)
      //   NAL type 1 = non-IDR (P/B frame)

      for (size_t i = 0; i + 4 < size; i++)
      {
        // Detect 4-byte start code: 0x00 0x00 0x00 0x01
        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
            data[i + 2] == 0x00 && data[i + 3] == 0x01)
        {
          uint8_t nal_type = data[i + 4] & 0x1F;
          if (nal_type == 5)
          { // IDR slice
            return "I-frame";
          }
          i += 3; // skip ahead
        }
        // Detect 3-byte start code: 0x00 0x00 0x01
        else if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01)
        {
          uint8_t nal_type = data[i + 3] & 0x1F;
          if (nal_type == 5)
          { // IDR slice
            return "I-frame";
          }
          i += 2; // skip ahead
        }
      }
      return "P-frame";
    }

    static void thread_scheduler(const char *thread_name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
    {
      schedule_cfg->core_id = VENC_TASK_CORE_ID;
      schedule_cfg->stack_size = VENC_TASK_STACK_SIZE;
      schedule_cfg->priority = VENC_TASK_PRIORITY;
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

      esp_capture_cfg_t capture_cfg = {
          .sync_mode = ESP_CAPTURE_SYNC_MODE_SYSTEM,
          .audio_src = nullptr,
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

  VideoStreamWs *VideoStreamWs::instance_ = nullptr;

}
