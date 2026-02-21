#pragma once

#include <string>
#include <memory>
#include <functional>
#include "music_player.hpp"
#include "video_stream.hpp"
#include "logger.hpp"
#include "esp_system.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "task.hpp"

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
          logger_({.tag = "HttpCtrl", .level = espp::Logger::Verbosity::INFO}),
          video_stream_(std::make_unique<VideoStream>())
    {
      instance_ = this;
    }

    ~HttpController()
    {
      stop();
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
    std::unique_ptr<VideoStream> video_stream_;
    static HttpController *instance_;

    bool start_server()
    {
      logger_.info("Starting HTTP control server on {}:{}", config_.bind_address, config_.port);

      // Adjust http server log levels
      // esp_log_level_set("httpd", ESP_LOG_DEBUG);
      // esp_log_level_set("httpd_parse", ESP_LOG_DEBUG);
      // esp_log_level_set("httpd_txrx", ESP_LOG_DEBUG);

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

      esp_err_t ret = video_stream_->init();
      if (ret != ESP_OK)
      {
        logger_.error("Failed to initialize video stream");
        return false;
      }

      register_handlers();
      logger_.info("HTTP control server started successfully");
      return true;
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

      // Video stream endpoint
      register_uri("/stream.h264", HTTP_GET, VideoStream::handle_stream);
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
      httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
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

      auto start = esp_timer_get_time(); // microseconds

      auto signal_strength = wifi::get_signal_strength();

      cJSON *root = cJSON_CreateObject();
      cJSON_AddNumberToObject(root, "wifi_signal_strength", signal_strength);
      send_json(req, root);

      auto elapsed_ms = (esp_timer_get_time() - start) / 1000.0f;
      instance_->logger_.info("handle_signal_info took {:.2f} ms", elapsed_ms);

      return ESP_OK;
    }
  };

  HttpController *HttpController::instance_ = nullptr;
}
