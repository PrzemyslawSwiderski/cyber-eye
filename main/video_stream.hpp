#pragma once

#include <mutex>
#include "logger.hpp"
#include "esp_system.h"
#include "esp_http_server.h"

#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_video_enc_default.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_capture_advance.h"
#include "settings.h"

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
      httpd_resp_set_type(req, "video/h264");
      httpd_resp_set_status(req, "200 OK");
      httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
      httpd_resp_set_hdr(req, "Pragma", "no-cache");
      httpd_resp_set_hdr(req, "Expires", "0");
      httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
      httpd_resp_set_hdr(req, "Accept-Ranges", "none");
      httpd_resp_set_hdr(req, "Transfer-Encoding", "chunked");
      httpd_resp_set_hdr(req, "Connection", "close");

      instance_->logger_.info("Client connected to stream");

      esp_capture_stream_frame_t frame = {};
      frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;

      int frame_count = 0;
      int dropped_count = 0;

      while (true)
      {
        esp_err_t err = esp_capture_sink_acquire_frame(instance_->stream_sink_, &frame, true);

        if (err != ESP_CAPTURE_ERR_OK)
        {
          dropped_count++;
          esp_capture_sink_release_frame(instance_->stream_sink_, &frame);
          continue;
        }

        dropped_count = 0;

        esp_err_t ret = httpd_resp_send_chunk(req, (const char *)frame.data, frame.size);

        esp_capture_sink_release_frame(instance_->stream_sink_, &frame);

        if (ret != ESP_OK)
        {
          instance_->logger_.info("Client disconnected, ret={}", ret);
          break;
        }

        frame_count++;
      }

      instance_->logger_.info("Stream ended, total frames={}, dropped={}", frame_count, dropped_count);

      httpd_resp_send_chunk(req, NULL, 0);
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

    static void thread_scheduler(const char *thread_name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
    {
      schedule_cfg->core_id = 1;
      schedule_cfg->stack_size = 40 * 1024;
      schedule_cfg->priority = 1;
    }

    static esp_capture_video_src_if_t *create_video_source()
    {
      esp_capture_video_v4l2_src_cfg_t v4l2_cfg = {
          .dev_name = "/dev/video0",
          .buf_count = 4,
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
