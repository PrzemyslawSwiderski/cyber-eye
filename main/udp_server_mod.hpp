#pragma once

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "time.h"

class UDPServer
{
private:
  static const char *TAG;
  static TaskHandle_t data_task_handle;
  static TaskHandle_t control_task_handle;
  static uint16_t data_port;
  static uint16_t control_port;
  static bool is_running;
  static bool stream_active;
  static struct sockaddr_in client_addr;
  static SemaphoreHandle_t stream_mutex;

  // Data task - sends timestamps (lower priority)
  static void data_task(void *pvParameters)
  {
    struct sockaddr_in dest_addr;
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(data_port);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
      ESP_LOGE(TAG, "Unable to create data socket");
      vTaskDelete(NULL);
      return;
    }

    if (bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
    {
      ESP_LOGE(TAG, "Unable to bind data socket");
      close(sock);
      vTaskDelete(NULL);
      return;
    }

    ESP_LOGI(TAG, "Data server bound to port %d", data_port);
    char buffer[128];

    while (is_running)
    {
      bool send_data = false;

      // Check if streaming is active
      xSemaphoreTake(stream_mutex, portMAX_DELAY);
      send_data = stream_active;
      xSemaphoreGive(stream_mutex);

      if (send_data && client_addr.sin_port != 0)
      {
        // Get current timestamp
        struct timeval tv;
        gettimeofday(&tv, NULL);

        // Format: seconds.microseconds
        snprintf(buffer, sizeof(buffer), "%.3f", tv.tv_sec + (tv.tv_usec / 1000000.0));

        int sent = sendto(sock, buffer, strlen(buffer), 0,
                          (struct sockaddr *)&client_addr, sizeof(client_addr));

        if (sent < 0)
        {
          ESP_LOGW(TAG, "Failed to send timestamp - client may have disconnected");
          xSemaphoreTake(stream_mutex, portMAX_DELAY);
          stream_active = false;
          xSemaphoreGive(stream_mutex);
        }
        else
        {
          ESP_LOGI(TAG, "Sent timestamp: %s", buffer);
        }

        vTaskDelay(pdMS_TO_TICKS(17)); // ~60 FPS
      }
      else
      {
        // No streaming active, wait a bit
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }

    close(sock);
    data_task_handle = nullptr;
    vTaskDelete(NULL);
  }

  // Control task - handles start/stop commands (higher priority)
  static void control_task(void *pvParameters)
  {
    struct sockaddr_in dest_addr;
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(control_port);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
      ESP_LOGE(TAG, "Unable to create control socket");
      vTaskDelete(NULL);
      return;
    }

    if (bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
    {
      ESP_LOGE(TAG, "Unable to bind control socket");
      close(sock);
      vTaskDelete(NULL);
      return;
    }

    ESP_LOGI(TAG, "Control server bound to port %d", control_port);
    char buffer[32];

    while (is_running)
    {
      int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&source_addr, &socklen);

      if (len > 0)
      {
        buffer[len] = '\0';
        char client_ip[16];
        inet_ntoa_r(source_addr.sin_addr, client_ip, sizeof(client_ip));

        if (strcmp(buffer, "start") == 0)
        {
          xSemaphoreTake(stream_mutex, portMAX_DELAY);
          client_addr = source_addr;
          stream_active = true;
          xSemaphoreGive(stream_mutex);

          ESP_LOGI(TAG, "START command from %s:%d - streaming started",
                   client_ip, ntohs(source_addr.sin_port));

          // Send confirmation
          const char *ack = "started";
          sendto(sock, ack, strlen(ack), 0,
                 (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
        else if (strcmp(buffer, "stop") == 0)
        {
          xSemaphoreTake(stream_mutex, portMAX_DELAY);
          stream_active = false;
          xSemaphoreGive(stream_mutex);

          ESP_LOGI(TAG, "STOP command from %s:%d - streaming stopped",
                   client_ip, ntohs(source_addr.sin_port));

          // Send confirmation
          const char *ack = "stopped";
          sendto(sock, ack, strlen(ack), 0,
                 (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
        else if (strcmp(buffer, "status") == 0)
        {
          xSemaphoreTake(stream_mutex, portMAX_DELAY);
          const char *status = stream_active ? "streaming" : "stopped";
          xSemaphoreGive(stream_mutex);

          sendto(sock, status, strlen(status), 0,
                 (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
        else
        {
          ESP_LOGW(TAG, "Unknown command: %s from %s:%d",
                   buffer, client_ip, ntohs(source_addr.sin_port));
        }
      }

      vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent CPU hogging
    }

    close(sock);
    control_task_handle = nullptr;
    vTaskDelete(NULL);
  }

public:
  static esp_err_t start(uint16_t data_port_num = 3333, uint16_t control_port_num = 3334)
  {
    if (is_running)
      return ESP_ERR_INVALID_STATE;

    data_port = data_port_num;
    control_port = control_port_num;
    is_running = true;
    stream_active = false;
    memset(&client_addr, 0, sizeof(client_addr));

    // Create mutex for thread-safe access to shared variables
    stream_mutex = xSemaphoreCreateMutex();
    if (stream_mutex == NULL)
    {
      ESP_LOGE(TAG, "Failed to create mutex");
      return ESP_ERR_NO_MEM;
    }

    // Create data task (lower priority)
    xTaskCreate(data_task, "udp_data", 4096, nullptr, 3, &data_task_handle);

    // Create control task (higher priority)
    xTaskCreate(control_task, "udp_control", 4096, nullptr, 6, &control_task_handle);

    ESP_LOGI(TAG, "UDP server started - Data port: %d, Control port: %d", data_port, control_port);
    return ESP_OK;
  }

  static void stop()
  {
    is_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));

    if (stream_mutex != NULL)
    {
      vSemaphoreDelete(stream_mutex);
      stream_mutex = NULL;
    }

    ESP_LOGI(TAG, "UDP server stopped");
  }

  static void start_stream()
  {
    xSemaphoreTake(stream_mutex, portMAX_DELAY);
    stream_active = true;
    xSemaphoreGive(stream_mutex);
    ESP_LOGI(TAG, "Stream started manually");
  }

  static void stop_stream()
  {
    xSemaphoreTake(stream_mutex, portMAX_DELAY);
    stream_active = false;
    xSemaphoreGive(stream_mutex);
    ESP_LOGI(TAG, "Stream stopped manually");
  }

  static bool is_streaming()
  {
    bool active;
    xSemaphoreTake(stream_mutex, portMAX_DELAY);
    active = stream_active;
    xSemaphoreGive(stream_mutex);
    return active;
  }

  static uint32_t get_frame_count()
  {
    return 0;
  }

  static uint16_t get_data_port() { return data_port; }
  static uint16_t get_control_port() { return control_port; }
};

const char *UDPServer::TAG = "udp_server";
TaskHandle_t UDPServer::data_task_handle = nullptr;
TaskHandle_t UDPServer::control_task_handle = nullptr;
uint16_t UDPServer::data_port = 3333;
uint16_t UDPServer::control_port = 3334;
bool UDPServer::is_running = false;
bool UDPServer::stream_active = false;
struct sockaddr_in UDPServer::client_addr = {};
SemaphoreHandle_t UDPServer::stream_mutex = nullptr;
