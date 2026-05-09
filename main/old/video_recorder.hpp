#pragma once

#include <mutex>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "logger.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_video_enc_default.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_capture_advance.h"

#define CAMERA_DEVICE_NAME "/dev/video0"
#define VENC_TASK_STACK_SIZE (16 * 1024)
#define VENC_TASK_PRIORITY 14
#define VENC_TASK_CORE_ID 1

#define BENCH_TASK_STACK_SIZE (8 * 1024)
#define BENCH_TASK_PRIORITY 12
#define BENCH_TASK_CORE_ID 1

namespace ctrl
{
  class VideoRecorder
  {
  public:
    explicit VideoRecorder()
        : logger_({.tag = "VideoRec", .level = espp::Logger::Verbosity::INFO})
    {
      instance_ = this;
    }

    ~VideoRecorder() { stop(); }

    esp_err_t init()
    {
      std::lock_guard<std::mutex> lock(mutex_);

      esp_video_enc_register_default();
      esp_capture_set_thread_scheduler(thread_scheduler);

      memset(&capture_sys_, 0, sizeof(capture_sys_));

      if (build_capture(&capture_sys_) != 0)
      {
        logger_.error("Failed to build capture");
        return ESP_FAIL;
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

      esp_err_t ret = esp_capture_sink_setup(capture_sys_.capture, 0, &sink_cfg, &sink_);
      if (ret != ESP_CAPTURE_ERR_OK)
      {
        logger_.error("Failed to setup sink: {}", ret);
        destroy_capture(&capture_sys_);
        return ret;
      }

      esp_capture_sink_enable(sink_, ESP_CAPTURE_RUN_MODE_ALWAYS);

      ret = esp_capture_start(capture_sys_.capture);
      if (ret != ESP_CAPTURE_ERR_OK)
      {
        logger_.error("Failed to start capture: {}", ret);
        destroy_capture(&capture_sys_);
        return ret;
      }

      logger_.info("Capture initialized, {}x{} @ {} fps",
                   CONFIG_VIDEO_STREAM_WIDTH, CONFIG_VIDEO_STREAM_HEIGHT, CONFIG_VIDEO_STREAM_FPS);
      return ESP_OK;
    }

    // Start a background task that acquires frames and logs throughput
    void start_benchmark(uint32_t duration_s = 30)
    {
      auto *ctx = new BenchCtx{.sink = sink_, .duration_s = duration_s};
      xTaskCreatePinnedToCore(
          bench_task, "vid_bench",
          BENCH_TASK_STACK_SIZE, ctx,
          BENCH_TASK_PRIORITY, &bench_task_handle_,
          BENCH_TASK_CORE_ID);
    }

    void stop()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (bench_task_handle_)
      {
        vTaskDelete(bench_task_handle_);
        bench_task_handle_ = nullptr;
      }
      esp_capture_stop(capture_sys_.capture);
      destroy_capture(&capture_sys_);
      sink_ = nullptr;
      logger_.info("Capture stopped");
    }

  private:
    typedef struct
    {
      esp_capture_handle_t capture;
      esp_capture_video_src_if_t *vid_src;
    } capture_sys_t;

    struct BenchCtx
    {
      esp_capture_sink_handle_t sink;
      uint32_t duration_s;
    };

    espp::Logger logger_;
    capture_sys_t capture_sys_{};
    esp_capture_sink_handle_t sink_{nullptr};
    TaskHandle_t bench_task_handle_{nullptr};
    std::mutex mutex_;
    static VideoRecorder *instance_;

    // ── benchmark task ───────────────────────────────────────────────────────
    static void bench_task(void *arg)
    {
      auto *ctx = static_cast<BenchCtx *>(arg);

      int total_frames = 0;
      int dropped_frames = 0;
      int fps_frames = 0;
      int64_t fps_last_us = esp_timer_get_time();
      int64_t start_us = fps_last_us;
      int64_t deadline_us = start_us + ctx->duration_s * 1'000'000LL;

      instance_->logger_.info("Benchmark started, duration={}s", ctx->duration_s);

      esp_capture_stream_frame_t frame = {};
      frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;

      while (esp_timer_get_time() < deadline_us)
      {
        esp_err_t err = esp_capture_sink_acquire_frame(ctx->sink, &frame, false);
        if (err != ESP_CAPTURE_ERR_OK)
        {
          dropped_frames++;
          esp_capture_sink_release_frame(ctx->sink, &frame);
          vTaskDelay(pdMS_TO_TICKS(1));
          continue;
        }

        // Frame acquired — measure size, immediately release (no send)
        uint32_t frame_size = frame.size;
        esp_capture_sink_release_frame(ctx->sink, &frame);

        total_frames++;
        fps_frames++;

        // Log every 3 s
        int64_t now = esp_timer_get_time();
        int64_t elapsed_us = now - fps_last_us;
        if (elapsed_us >= 3'000'000)
        {
          float fps = fps_frames / (elapsed_us / 1'000'000.0f);
          instance_->logger_.info(
              "FPS: {:.1f}  last_frame={}B  total={}  dropped={}",
              fps, frame_size, total_frames, dropped_frames);
          fps_frames = 0;
          fps_last_us = now;
        }
      }

      // Summary
      float total_s = (esp_timer_get_time() - start_us) / 1'000'000.0f;
      float avg_fps = total_frames / total_s;
      float drop_rate = dropped_frames * 100.0f / (total_frames + dropped_frames);
      instance_->logger_.info(
          "Benchmark done: avg_fps={:.1f}  total={}  dropped={}  drop_rate={:.1f}%",
          avg_fps, total_frames, dropped_frames, drop_rate);

      delete ctx;
      vTaskDelete(nullptr);
    }

    // ── encoder thread scheduler ─────────────────────────────────────────────
    static void thread_scheduler(const char *thread_name,
                                 esp_capture_thread_schedule_cfg_t *cfg)
    {
      cfg->core_id = VENC_TASK_CORE_ID;
      cfg->stack_size = VENC_TASK_STACK_SIZE;
      cfg->priority = VENC_TASK_PRIORITY;
    }

    // ── camera controls ──────────────────────────────────────────────────────
    static void set_camera_controls(const char *dev_name)
    {
      int fd = open(dev_name, O_RDWR);
      if (fd < 0)
        return;

      auto set_ctrl = [fd](uint32_t id, int32_t value)
      {
        struct v4l2_control ctrl = {.id = id, .value = value};
        ioctl(fd, VIDIOC_S_CTRL, &ctrl);
      };

      set_ctrl(V4L2_CID_BRIGHTNESS, 60);
      set_ctrl(V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL);
      set_ctrl(V4L2_CID_EXPOSURE_ABSOLUTE, 500);
      set_ctrl(V4L2_CID_GAIN, 80);
      set_ctrl(V4L2_CID_CONTRAST, 30);

      close(fd);
    }

    // ── capture lifecycle ────────────────────────────────────────────────────
    static esp_capture_video_src_if_t *create_video_source()
    {
      esp_capture_video_v4l2_src_cfg_t cfg = {
          .dev_name = CAMERA_DEVICE_NAME,
          .buf_count = 8,
      };
      return esp_capture_new_video_v4l2_src(&cfg);
    }

    static int build_capture(capture_sys_t *sys)
    {
      sys->vid_src = create_video_source();
      if (!sys->vid_src)
      {
        instance_->logger_.error("Failed to create video source");
        return -1;
      }
      set_camera_controls(CAMERA_DEVICE_NAME);

      esp_capture_cfg_t cfg = {
          .sync_mode = ESP_CAPTURE_SYNC_MODE_SYSTEM,
          .audio_src = nullptr,
          .video_src = sys->vid_src,
      };
      esp_capture_open(&cfg, &sys->capture);
      if (!sys->capture)
      {
        instance_->logger_.error("Failed to open capture");
        return -1;
      }
      return 0;
    }

    static void destroy_capture(capture_sys_t *sys)
    {
      if (sys->capture)
      {
        esp_capture_close(sys->capture);
        sys->capture = nullptr;
      }
      if (sys->vid_src)
      {
        free(sys->vid_src);
        sys->vid_src = nullptr;
      }
    }
  };

  VideoRecorder *VideoRecorder::instance_ = nullptr;

} // namespace ctrl
