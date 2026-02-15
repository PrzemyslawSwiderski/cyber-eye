#pragma once

#include <string>
#include <memory>
#include <functional>
#include "music_player.hpp"
#include "logger.hpp"
#include "esp_system.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "task.hpp"

#include "ts_muxer.h"
#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_video_enc_default.h"
#include "esp_audio_enc_default.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_capture_advance.h"
#include "settings.h"

namespace ctrl
{
  class HttpController
  {
  public:
    struct Config
    {
      uint16_t port;
      std::string bind_address;

      Config()
          : port(8080),
            bind_address("0.0.0.0") {}
    };

    explicit HttpController(audio::MusicPlayer *player, Config cfg = Config())
        : player_(player),
          config_(cfg),
          logger_({.tag = "HttpCtrl", .level = espp::Logger::Verbosity::INFO})
    {
      instance_ = this;
    }

    ~HttpController()
    {
      stop();
      cleanup_video_capture();
    }

    void start_task()
    {
      if (http_server_)
      {
        logger_.warn("HTTP server already running");
        return;
      }

      start_server();
    }

    void stop()
    {
      logger_.info("Stopping HTTP control server");
      if (http_server_)
      {
        httpd_stop(http_server_);
        http_server_ = nullptr;
      }
    }

  private:
    audio::MusicPlayer *player_;
    Config config_;
    espp::Logger logger_;
    httpd_handle_t http_server_{nullptr};
    static HttpController *instance_;

    typedef struct
    {
      esp_capture_handle_t capture;        /*!< Capture handle */
      esp_capture_audio_src_if_t *aud_src; /*!< Audio source interface for video capture */
      esp_capture_video_src_if_t *vid_src; /*!< Video source interface */
    } video_capture_sys_t;

    typedef struct
    {
      uint32_t aud_frames;
      uint32_t aud_total_frame_size;
      uint32_t vid_frames;
      uint32_t vid_total_frame_size;
    } video_capture_res_t;

    video_capture_sys_t capture_sys_;
    esp_capture_sink_handle_t stream_sink_;
    std::mutex capture_mutex_; // To handle concurrent access

    bool start_server()
    {
      logger_.info("Starting HTTP control server on {}:{}", config_.bind_address, config_.port);

      // Adjust http server log levels
      esp_log_level_set("httpd", ESP_LOG_DEBUG);
      esp_log_level_set("httpd_parse", ESP_LOG_DEBUG);
      esp_log_level_set("httpd_txrx", ESP_LOG_DEBUG);

      httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
      http_config.server_port = config_.port;
      http_config.max_uri_handlers = 20;
      http_config.task_priority = 5;
      http_config.stack_size = 40 * 1024;
      http_config.core_id = 0;

      if (httpd_start(&http_server_, &http_config) != ESP_OK)
      {
        logger_.error("Failed to start HTTP server");
        return false;
      }

      // Initialize capture system if not already done
      esp_err_t ret = instance_->init_video_capture();
      if (ret != ESP_OK)
      {
        logger_.error("init_video_capture");
        return ESP_FAIL;
      }

      register_handlers();
      logger_.info("HTTP control server started successfully");
      return true;
    }

    static void capture_test_scheduler(const char *thread_name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
    {
      if (strcmp(thread_name, "buffer_in") == 0)
      {
        // AEC feed task can have high priority
        schedule_cfg->stack_size = 6 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
      }
      else if (strcmp(thread_name, "venc_0") == 0)
      {
        // For H264 may need huge stack if use hardware encoder can set it to small value
        schedule_cfg->core_id = 1;
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 1;
      }
    }

    esp_err_t init_video_capture()
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);

      esp_video_enc_register_default();

      esp_capture_set_thread_scheduler(capture_test_scheduler);

      memset(&capture_sys_, 0, sizeof(capture_sys_));

      esp_err_t ret = build_video_capture(&capture_sys_);
      if (ret != ESP_OK)
      {
        logger_.error("Failed to build video capture: {}", ret);
        return ret;
      }

      // Setup sink
      esp_capture_sink_cfg_t sink_cfg = {
          .video_info = {
              .format_id = VIDEO_SINK0_FMT,
              .width = VIDEO_SINK0_WIDTH,
              .height = VIDEO_SINK0_HEIGHT,
              .fps = VIDEO_SINK0_FPS,
          },
      };

      ret = esp_capture_sink_setup(capture_sys_.capture, 0, &sink_cfg, &stream_sink_);
      if (ret != ESP_CAPTURE_ERR_OK)
      {
        logger_.error("Failed to setup sink: {}", ret);
        destroy_video_capture(&capture_sys_);
        return ret;
      }

      // esp_capture_sink_enable_muxer(stream_sink_, true);
      esp_capture_sink_enable(stream_sink_, ESP_CAPTURE_RUN_MODE_ALWAYS);

      // Start capture once
      ret = esp_capture_start(capture_sys_.capture);
      if (ret != ESP_CAPTURE_ERR_OK)
      {
        logger_.error("Failed to start capture: {}", ret);
        destroy_video_capture(&capture_sys_);
        return ret;
      }

      logger_.info("Video capture initialized successfully");

      // Wait for muxer to initialize
      vTaskDelay(pdMS_TO_TICKS(500));

      return ESP_OK;
    }

    void cleanup_video_capture()
    {
      std::lock_guard<std::mutex> lock(capture_mutex_);

      esp_capture_stop(capture_sys_.capture);
      destroy_video_capture(&capture_sys_);

      stream_sink_ = nullptr;

      logger_.info("Video capture cleaned up");
    }

    void register_handlers()
    {
      // Music endpoints
      register_uri("/api/music/play", HTTP_GET, handle_music_play);
      register_uri("/api/music/stop", HTTP_GET, handle_music_stop);
      register_uri("/api/music/volume", HTTP_GET, handle_music_volume);
      register_uri("/api/music/status", HTTP_GET, handle_music_status);

      // System endpoints
      register_uri("/api/system/tasks", HTTP_GET, handle_system_tasks);
      register_uri("/api/system/info", HTTP_GET, handle_system_info);
      register_uri("/api/system/reset", HTTP_GET, handle_system_reset);
      register_uri("/api/signal/info", HTTP_GET, handle_signal_info);

      register_uri("/stream.h264", HTTP_GET, handle_stream);
    }

    void register_uri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
    {
      httpd_uri_t uri_config = {
          .uri = uri,
          .method = method,
          .handler = handler,
          .user_ctx = nullptr};
      httpd_register_uri_handler(http_server_, &uri_config);
    }

    // Helper: Log incoming request
    static void log_request(httpd_req_t *req, const char *endpoint)
    {
      instance_->logger_.info("Request: {} {}",
                              req->method == HTTP_GET ? "GET" : req->method == HTTP_POST ? "POST"
                                                                                         : "OTHER",
                              endpoint);
    }

    // Helper: Get query parameter
    static bool get_query_param(httpd_req_t *req, const char *key, char *value, size_t value_size)
    {
      char buf[256];
      if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK)
      {
        return false;
      }
      return httpd_query_key_value(buf, key, value, value_size) == ESP_OK;
    }

    // Helper: Send JSON response
    static void send_json(httpd_req_t *req, cJSON *root)
    {
      char *json_str = cJSON_Print(root);
      httpd_resp_set_type(req, "application/json");
      httpd_resp_sendstr(req, json_str);
      free(json_str);
      cJSON_Delete(root);
    }

    // Helper: Send simple JSON message
    static void send_json_message(httpd_req_t *req, const char *key, const char *value)
    {
      cJSON *root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, key, value);
      send_json(req, root);
    }

    // ========== Music Endpoints ==========

    static esp_err_t handle_music_play(httpd_req_t *req)
    {
      log_request(req, "/api/music/play");

      char file_param[256] = {0};
      if (!get_query_param(req, "file", file_param, sizeof(file_param)))
      {
        instance_->logger_.warn("Play request missing 'file' parameter");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'file' query parameter");
        return ESP_FAIL;
      }

      instance_->logger_.info("Playing file: {}", file_param);
      instance_->player_->play(file_param);
      send_json_message(req, "status", "playing");
      return ESP_OK;
    }

    static esp_err_t handle_music_stop(httpd_req_t *req)
    {
      log_request(req, "/api/music/stop");
      instance_->logger_.info("Stopping playback");
      instance_->player_->stop();
      send_json_message(req, "status", "stopped");
      return ESP_OK;
    }

    static esp_err_t handle_music_volume(httpd_req_t *req)
    {
      log_request(req, "/api/music/volume");

      char value_param[8] = {0};
      if (!get_query_param(req, "value", value_param, sizeof(value_param)))
      {
        instance_->logger_.warn("Volume request missing 'value' parameter");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'value' query parameter");
        return ESP_FAIL;
      }

      int vol = atoi(value_param);
      if (vol < 0 || vol > 100)
      {
        instance_->logger_.warn("Invalid volume value: {}", vol);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Volume must be between 0-100");
        return ESP_FAIL;
      }

      instance_->logger_.info("Setting volume to {}%", vol);
      instance_->player_->set_volume((uint8_t)vol);
      send_json_message(req, "status", "volume_set");
      return ESP_OK;
    }

    static esp_err_t handle_music_status(httpd_req_t *req)
    {
      log_request(req, "/api/music/status");

      audio::PlayerState state = instance_->player_->state();
      const char *state_str = [state]()
      {
        switch (state)
        {
        case audio::PlayerState::IDLE:
          return "idle";
        case audio::PlayerState::PLAYING:
          return "playing";
        case audio::PlayerState::STOPPED:
          return "stopped";
        case audio::PlayerState::ERROR:
          return "error";
        default:
          return "unknown";
        }
      }();

      instance_->logger_.debug("Player state: {}", state_str);
      send_json_message(req, "state", state_str);
      return ESP_OK;
    }

    // ========== System Endpoints ==========

    static esp_err_t handle_system_tasks(httpd_req_t *req)
    {
      log_request(req, "/api/system/tasks");

      cJSON *root = cJSON_CreateObject();
      cJSON *tasks = cJSON_CreateArray();

      UBaseType_t task_count = uxTaskGetNumberOfTasks();
      TaskStatus_t *task_status = (TaskStatus_t *)malloc(task_count * sizeof(TaskStatus_t));

      if (task_status)
      {
        task_count = uxTaskGetSystemState(task_status, task_count, nullptr);
        instance_->logger_.debug("Found {} tasks", task_count);

        for (UBaseType_t i = 0; i < task_count; i++)
        {
          cJSON *task = cJSON_CreateObject();
          cJSON_AddStringToObject(task, "name", task_status[i].pcTaskName);
          cJSON_AddNumberToObject(task, "priority", task_status[i].uxCurrentPriority);
          cJSON_AddNumberToObject(task, "stack_hwm", task_status[i].usStackHighWaterMark);
          cJSON_AddNumberToObject(task, "state", task_status[i].eCurrentState);
          cJSON_AddItemToArray(tasks, task);
        }
        free(task_status);
      }

      cJSON_AddNumberToObject(root, "task_count", task_count);
      cJSON_AddItemToObject(root, "tasks", tasks);
      send_json(req, root);
      return ESP_OK;
    }

    static esp_err_t handle_system_info(httpd_req_t *req)
    {
      log_request(req, "/api/system/info");

      uint32_t heap_free = esp_get_free_heap_size();
      uint32_t heap_min = esp_get_minimum_free_heap_size();
      uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

      instance_->logger_.debug("Heap: free={}, min={}, uptime={}ms",
                               heap_free, heap_min, uptime_ms);

      cJSON *root = cJSON_CreateObject();
      cJSON_AddNumberToObject(root, "heap_free", heap_free);
      cJSON_AddNumberToObject(root, "heap_min", heap_min);
      cJSON_AddNumberToObject(root, "uptime_ms", uptime_ms);
      send_json(req, root);
      return ESP_OK;
    }

    static esp_err_t handle_system_reset(httpd_req_t *req)
    {
      log_request(req, "/api/system/reset");

      esp_restart();

      send_json_message(req, "status", "restarting");
      return ESP_OK;
    }

    static esp_err_t handle_signal_info(httpd_req_t *req)
    {
      log_request(req, "/api/signal/info");

      auto signal_strength = wifi::get_signal_strength();
      instance_->logger_.debug("WiFi signal strength: {}", signal_strength);

      cJSON *root = cJSON_CreateObject();
      cJSON_AddNumberToObject(root, "wifi_signal_strength", signal_strength);
      send_json(req, root);
      return ESP_OK;
    }

    static esp_capture_video_src_if_t *create_video_source(void)
    {
      esp_capture_video_v4l2_src_cfg_t v4l2_cfg = {
          .dev_name = "/dev/video0",
          .buf_count = 8,
      };
      return esp_capture_new_video_v4l2_src(&v4l2_cfg);
    }

    static int build_video_capture(video_capture_sys_t *capture_sys)
    {
      // Create video source firstly
      capture_sys->vid_src = create_video_source();
      if (capture_sys->vid_src == NULL)
      {
        instance_->logger_.info("Fail to create video source");
        return -1;
      }
      esp_capture_cfg_t capture_cfg = {
          .sync_mode = ESP_CAPTURE_SYNC_MODE_SYSTEM,
          .video_src = capture_sys->vid_src,
      };
      esp_capture_open(&capture_cfg, &capture_sys->capture);
      if (capture_sys->capture == NULL)
      {
        instance_->logger_.info("Fail to create capture");
        return -1;
      }
      return 0;
    }

    static esp_err_t handle_stream(httpd_req_t *req)
    {
      log_request(req, "/stream.h264");

      // HTTP headers
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

      // Stream loop
      esp_capture_stream_frame_t frame = {};
      frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;

      int frame_count = 0;
      int dropped_count = 0;

      // Acquire video frame with timeout
      while (true)
      {
        esp_err_t err = esp_capture_sink_acquire_frame(instance_->stream_sink_, &frame, true);

        if (err != ESP_CAPTURE_ERR_OK)
        {
          // instance_->logger_.error("Frame acquire failed: error code={}", err);
          dropped_count++;
          esp_capture_sink_release_frame(instance_->stream_sink_, &frame);
          continue;
        }

        dropped_count = 0; // Reset on success

        if (frame_count == 0)
        {
          instance_->logger_.info("First 16 bytes: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                                  frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                                  frame.data[4], frame.data[5], frame.data[6], frame.data[7],
                                  frame.data[8], frame.data[9], frame.data[10], frame.data[11],
                                  frame.data[12], frame.data[13], frame.data[14], frame.data[15]);
        }

        // instance_->logger_.info("Sending frame {}, size={}", frame_count, frame.size);
        esp_err_t ret = httpd_resp_send_chunk(req, (const char *)frame.data, frame.size);
        // instance_->logger_.info("Chunk sent, ret={}", ret);

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

    static void destroy_video_capture(video_capture_sys_t *capture_sys)
    {
      if (capture_sys->capture)
      {
        esp_capture_close(capture_sys->capture);
        capture_sys->capture = NULL;
      }
      if (capture_sys->aud_src)
      {
        free(capture_sys->aud_src);
        capture_sys->aud_src = NULL;
      }
      if (capture_sys->vid_src)
      {
        free(capture_sys->vid_src);
        capture_sys->vid_src = NULL;
      }
    }
  };

  // Static member initialization
  HttpController *HttpController::instance_ = nullptr;

} // namespace ctrl
