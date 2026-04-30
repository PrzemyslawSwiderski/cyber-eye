#pragma once

#include <string.h>
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

class HTTPController
{
private:
  static const char *TAG;
  static httpd_handle_t server;
  static bool is_running;

  // Callbacks for controlling the stream
  static void (*start_stream_callback)();
  static void (*stop_stream_callback)();
  static bool (*is_streaming_callback)();
  static uint32_t (*get_frame_count_callback)();

  // POST handler for /command
  static esp_err_t command_post_handler(httpd_req_t *req)
  {
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0)
    {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
      return ESP_FAIL;
    }

    content[ret] = 0;
    cJSON *json = cJSON_Parse(content);

    if (!json)
    {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
      return ESP_FAIL;
    }

    cJSON *command = cJSON_GetObjectItem(json, "command");
    cJSON *response = cJSON_CreateObject();

    if (command && cJSON_IsString(command))
    {
      const char *cmd = command->valuestring;

      if (strcmp(cmd, "start") == 0)
      {
        if (start_stream_callback)
          start_stream_callback();
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "Stream started");
      }
      else if (strcmp(cmd, "stop") == 0)
      {
        if (stop_stream_callback)
          stop_stream_callback();
        cJSON_AddStringToObject(response, "status", "success");
        cJSON_AddStringToObject(response, "message", "Stream stopped");
      }
      else if (strcmp(cmd, "status") == 0)
      {
        cJSON_AddBoolToObject(response, "streaming",
                              is_streaming_callback ? is_streaming_callback() : false);
        cJSON_AddNumberToObject(response, "frame_count",
                                get_frame_count_callback ? get_frame_count_callback() : 0);
      }
      else
      {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "message", "Unknown command");
      }
    }
    else
    {
      cJSON_AddStringToObject(response, "status", "error");
      cJSON_AddStringToObject(response, "message", "Missing command field");
    }

    char *response_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));

    cJSON_Delete(json);
    cJSON_Delete(response);
    free(response_str);
    return ESP_OK;
  }

public:
  static void set_callbacks(void (*start_cb)(), void (*stop_cb)(),
                            bool (*streaming_cb)(), uint32_t (*frames_cb)())
  {
    start_stream_callback = start_cb;
    stop_stream_callback = stop_cb;
    is_streaming_callback = streaming_cb;
    get_frame_count_callback = frames_cb;
  }

  static esp_err_t start(uint16_t http_port = 80)
  {
    if (is_running)
    {
      ESP_LOGW(TAG, "HTTP server already running");
      return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = http_port;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK)
    {
      httpd_uri_t command_uri = {
          .uri = "/command",
          .method = HTTP_POST,
          .handler = command_post_handler,
          .user_ctx = NULL};
      httpd_register_uri_handler(server, &command_uri);

      is_running = true;
      ESP_LOGI(TAG, "HTTP server started on port %d", http_port);
      ESP_LOGI(TAG, "Endpoint: POST /command");
      return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
  }

  static void stop()
  {
    if (!is_running)
      return;

    if (server)
    {
      httpd_stop(server);
      server = NULL;
    }

    is_running = false;
    ESP_LOGI(TAG, "HTTP server stopped");
  }

  static bool isActive()
  {
    return is_running;
  }
};

// Static member initialization
const char *HTTPController::TAG = "http_controller";
httpd_handle_t HTTPController::server = nullptr;
bool HTTPController::is_running = false;
void (*HTTPController::start_stream_callback)() = nullptr;
void (*HTTPController::stop_stream_callback)() = nullptr;
bool (*HTTPController::is_streaming_callback)() = nullptr;
uint32_t (*HTTPController::get_frame_count_callback)() = nullptr;
