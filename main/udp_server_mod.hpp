#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/igmp.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_netif.h"

// Clear the LwIP definitions of these macros
#undef _IO
#undef _IOR
#undef _IOW
#undef _IOWR
#include "video_mod.hpp"
#include "rtp_packetizer_mod.hpp"

class UDPH264Streamer
{
public:
  struct Config
  {
    uint16_t data_destination_port = 3333; // Port for video data
    uint16_t control_port = 3334;          // Port for control commands
    int task_priority = 7;                 // FreeRTOS task priority
    int task_stack_size = 8192;            // Stack size in bytes
  };

  static esp_err_t start(V4L2H264Capture *capture, const Config &config)
  {
    if (is_running_)
    {
      ESP_LOGW(TAG, "Streamer already running");
      return ESP_ERR_INVALID_STATE;
    }

    if (!capture)
    {
      ESP_LOGE(TAG, "Invalid capture device");
      return ESP_ERR_INVALID_ARG;
    }

    capture_ = capture;
    config_ = config;
    is_running_ = true;
    memset(&client_addr_, 0, sizeof(client_addr_));

    // Initialize RTP packetizer if enabled
    rtp_packetizer_ = new RTPPacketizer(esp_random());

    // Create mutex
    stream_mutex_ = xSemaphoreCreateMutex();
    if (stream_mutex_ == NULL)
    {
      ESP_LOGE(TAG, "Failed to create mutex");
      is_running_ = false;
      return ESP_ERR_NO_MEM;
    }

    // Start control task (handles start/stop/status commands)
    BaseType_t ret = xTaskCreate(controlTask, "udp_control",
                                 4096, nullptr, config_.task_priority + 1,
                                 &control_task_handle_);

    if (ret != pdPASS)
    {
      ESP_LOGE(TAG, "Failed to create control task");
      is_running_ = false;
      vSemaphoreDelete(stream_mutex_);
      return ESP_FAIL;
    }

    // Start data task (sends video frames)
    ret = xTaskCreate(dataTask, "udp_data",
                      config_.task_stack_size, nullptr, config_.task_priority,
                      &data_task_handle_);

    if (ret != pdPASS)
    {
      ESP_LOGE(TAG, "Failed to create data task");
      is_running_ = false;
      vTaskDelete(control_task_handle_);
      vSemaphoreDelete(stream_mutex_);
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UDP streamer started");
    return ESP_OK;
  }

  static void stop()
  {
    if (!is_running_)
    {
      return;
    }

    is_running_ = false;

    // Cleanup RTP packetizer
    if (rtp_packetizer_)
    {
      delete rtp_packetizer_;
      rtp_packetizer_ = nullptr;
    }

    // Wait for tasks to finish
    if (data_task_handle_)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      data_task_handle_ = nullptr;
    }

    if (control_task_handle_)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      control_task_handle_ = nullptr;
    }

    if (stream_mutex_)
    {
      vSemaphoreDelete(stream_mutex_);
      stream_mutex_ = nullptr;
    }

    ESP_LOGI(TAG, "Streamer stopped");
  }

private:
  static void sendFrame(int sock, const uint8_t *data, size_t size, struct sockaddr_in *destination_addr)
  {
    // Packetize the frame
    std::vector<std::vector<uint8_t>> packets = rtp_packetizer_->packetize(data, size);

    // Calculate expected size per packet type
    size_t total_packet_bytes = 0;
    for (const auto &packet : packets)
    {
      total_packet_bytes += packet.size();
    }

    for (size_t i = 0; i < packets.size(); i++)
    {
      const auto &packet = packets[i];
      int sent = -1;

      sent = sendto(sock, packet.data(), packet.size(), 0,
                    (struct sockaddr *)destination_addr, sizeof(*destination_addr));

      if (sent < 0)
      {
        ESP_LOGE(TAG, "Send error (packet %zu/%zu): %s (errno=%d)",
                 i + 1, packets.size(), strerror(errno), errno);
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  static void dataTask(void *pvParameters)
  {
    capture_->init();

    int sock = -1;

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
      ESP_LOGE(TAG, "Unable to create data socket");
    }

    // Set TOS for low delay
    int tos = 0xB8; // AF (Assured Forwarding) for video
    if (setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0)
    {
      ESP_LOGW(TAG, "Failed to set TOS");
    }

    // Reset RTP sequence when starting
    if (rtp_packetizer_)
    {
      rtp_packetizer_->resetSequence();
    }

    if (capture_->start() != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to start video capture");
    }

    uint32_t frame_count = 0;
    uint32_t last_log_time = 0;

    while (is_running_)
    {
      bool send_data = false;

      xSemaphoreTake(stream_mutex_, pdMS_TO_TICKS(100));
      send_data = stream_active_;
      xSemaphoreGive(stream_mutex_);

      if (send_data)
      {
        uint8_t *frame_data = nullptr;
        size_t frame_size = 0;
        uint32_t sequence;

        if (capture_->captureFrame(frame_data, frame_size, sequence))
        {
          sendFrame(sock, frame_data, frame_size, &client_addr_);

          frame_count++;

          // Periodic statistics
          uint32_t now = xTaskGetTickCount();
          if (now - last_log_time > pdMS_TO_TICKS(1000)) // Every second
          {
            ESP_LOGI(TAG, "Frames sent: %u ", frame_count);
            last_log_time = now;
            frame_count = 0;
          }
        }
        else
        {
          ESP_LOGW(TAG, "Could not capture frame");
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }
      else
      {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }

    ESP_LOGI(TAG, "dataTask closing. Sent %u frames total", frame_count);

    capture_->stop();

    if (sock >= 0)
    {
      close(sock);
    }
    data_task_handle_ = nullptr;
    vTaskDelete(NULL);
  }

  static void controlTask(void *pvParameters)
  {
    int sock = -1;
    struct sockaddr_in dest_addr = {};
    struct sockaddr_in source_addr = {};
    socklen_t socklen = sizeof(source_addr);

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(config_.control_port);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
      ESP_LOGE(TAG, "Unable to create control socket");
    }

    if (bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
    {
      ESP_LOGE(TAG, "Unable to bind control socket");
    }

    ESP_LOGI(TAG, "Control server bound to port %d", config_.control_port);
    char buffer[32];

    while (is_running_)
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
          client_addr_ = source_addr;
          // adjusting client port to fixed one
          client_addr_.sin_port = htons(config_.data_destination_port);

          xSemaphoreTake(stream_mutex_, portMAX_DELAY);
          stream_active_ = true;
          xSemaphoreGive(stream_mutex_);

          ESP_LOGI(TAG, "START command from %s:%d - streaming", client_ip, ntohs(source_addr.sin_port));

          const char *ack = "started";
          sendto(sock, ack, strlen(ack), 0,
                 (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
        else if (strcmp(buffer, "stop") == 0)
        {
          xSemaphoreTake(stream_mutex_, portMAX_DELAY);
          stream_active_ = false;
          xSemaphoreGive(stream_mutex_);

          ESP_LOGI(TAG, "STOP command from %s:%d - streaming stopped",
                   client_ip, ntohs(source_addr.sin_port));

          const char *ack = "stopped";
          sendto(sock, ack, strlen(ack), 0,
                 (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
        else if (strcmp(buffer, "status") == 0)
        {
          char status[128];
          xSemaphoreTake(stream_mutex_, portMAX_DELAY);

          if (stream_active_)
          {
            snprintf(status, sizeof(status), "streaming");
          }
          else
          {
            snprintf(status, sizeof(status), "stopped");
          }

          xSemaphoreGive(stream_mutex_);

          sendto(sock, status, strlen(status), 0,
                 (struct sockaddr *)&source_addr, sizeof(source_addr));

          ESP_LOGI(TAG, "STATUS query from %s:%d - %s",
                   client_ip, ntohs(source_addr.sin_port), status);
        }
        else
        {
          ESP_LOGW(TAG, "Unknown command: %s from %s:%d",
                   buffer, client_ip, ntohs(source_addr.sin_port));

          const char *ack = "unknown command";
          sendto(sock, ack, strlen(ack), 0,
                 (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (sock >= 0)
    {
      close(sock);
    }
    control_task_handle_ = nullptr;
    vTaskDelete(NULL);
  }

  static const char *TAG;
  static TaskHandle_t data_task_handle_;
  static TaskHandle_t control_task_handle_;
  static bool is_running_;
  static bool stream_active_;
  static struct sockaddr_in client_addr_;
  static V4L2H264Capture *capture_;
  static Config config_;
  static SemaphoreHandle_t stream_mutex_;
  static RTPPacketizer *rtp_packetizer_;
};

// Static member initialization
const char *UDPH264Streamer::TAG = "UDP_H264";
TaskHandle_t UDPH264Streamer::data_task_handle_ = nullptr;
TaskHandle_t UDPH264Streamer::control_task_handle_ = nullptr;
bool UDPH264Streamer::is_running_ = false;
bool UDPH264Streamer::stream_active_ = false;
struct sockaddr_in UDPH264Streamer::client_addr_ = {};
V4L2H264Capture *UDPH264Streamer::capture_ = nullptr;
UDPH264Streamer::Config UDPH264Streamer::config_ = {};
SemaphoreHandle_t UDPH264Streamer::stream_mutex_ = nullptr;
RTPPacketizer *UDPH264Streamer::rtp_packetizer_ = nullptr;
