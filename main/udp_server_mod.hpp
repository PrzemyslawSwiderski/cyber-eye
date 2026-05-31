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
#include "cmd_process_mod.hpp"

class UDPH264Streamer
{
public:
  static constexpr int SEND_MAX_RETRIES = 5;

  struct Config
  {
    uint16_t data_destination_port = 59227; // Port for video data
    uint16_t control_port = 3334;           // Port for control commands
    int task_priority = 20;                 // FreeRTOS task priority
    int task_stack_size = 8 * 1024;         // Stack size in bytes
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
    memset(&video_client_addr_, 0, sizeof(video_client_addr_));

    // Initialize command processor
    cmd_processor_ = new CmdProcessor();

    // Initialize RTP packetizer if enabled
    rtp_packetizer_ = new RTPPacketizer(esp_random());

    auto control_task_stack_size = 4 * 1024;
    // Start control task (handles start/stop/status commands)
    BaseType_t ret = xTaskCreatePinnedToCore(controlTask, "udp_control",
                                             control_task_stack_size,
                                             nullptr,
                                             config_.task_priority + 1,
                                             &control_task_handle_,
                                             1);

    if (ret != pdPASS)
    {
      ESP_LOGE(TAG, "Failed to create control task");
      is_running_ = false;
      return ESP_FAIL;
    }

    // Start data task (sends video frames)
    ret = xTaskCreatePinnedToCore(dataTask, "udp_data",
                                  config_.task_stack_size,
                                  nullptr,
                                  config_.task_priority,
                                  &data_task_handle_,
                                  1);

    if (ret != pdPASS)
    {
      ESP_LOGE(TAG, "Failed to create data task");
      is_running_ = false;
      vTaskDelete(control_task_handle_);
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

    // Cleanup command processor
    if (cmd_processor_)
    {
      delete cmd_processor_;
      cmd_processor_ = nullptr;
    }

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

    ESP_LOGI(TAG, "Streamer stopped");
  }

private:
  static void sendFrame(int sock, const uint8_t *data, size_t size, struct sockaddr_in *destination_addr)
  {
    // Packetize the frame
    uint64_t ts_us = esp_timer_get_time();
    std::vector<std::vector<uint8_t>> packets = rtp_packetizer_->packetize(data, size, ts_us);

    for (size_t i = 0; i < packets.size(); i++)
    {
      const auto &packet = packets[i];
      int sent = -1;
      int retries = 0;

      while (retries <= SEND_MAX_RETRIES)
      {
        sent = sendto(sock, packet.data(), packet.size(), 0,
                      (struct sockaddr *)destination_addr, sizeof(*destination_addr));
        if (sent > 0)
          break;

        if (errno == ENOMEM || errno == ENOBUFS)
        {
          taskYIELD();
          retries++;
        }
        else
        {
          ESP_LOGE(TAG, "Send error (packet %zu/%zu): errno=%d", i + 1, packets.size(), errno);
          return;
        }
      }

      if (sent <= 0)
      {
        ESP_LOGE(TAG, "Dropped packet %zu/%zu after %d retries (pool exhausted)",
                 i + 1, packets.size(), SEND_MAX_RETRIES);
      }
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

    while (is_running_)
    {
      if (stream_active_.load())
      {
        uint8_t *frame_data = nullptr;
        size_t frame_size = 0;
        uint32_t sequence;

        if (capture_->captureFrame(frame_data, frame_size, sequence))
        {
          sendFrame(sock, frame_data, frame_size, &video_client_addr_);
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

    ESP_LOGI(TAG, "dataTask closing.");

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
    char buffer[256];

    while (is_running_)
    {
      int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&source_addr, &socklen);

      if (len > 0)
      {
        buffer[len] = '\0';

        char source_ip[16];
        inet_ntoa_r(source_addr.sin_addr, source_ip, sizeof(source_ip));
        ESP_LOGI(TAG, "Command \"%s\" from %s:%d", buffer, source_ip, ntohs(source_addr.sin_port));

        if (cmd_processor_)
        {
          CmdProcessor::Context ctx{&stream_active_, &video_client_addr_, &source_addr, capture_};
          auto result = cmd_processor_->process(buffer, ctx);

          if (result.response)
          {
            ESP_LOGI(TAG, "Sending response: %s", result.response);
            sendto(sock, result.response, strlen(result.response), 0,
                   (struct sockaddr *)&source_addr, sizeof(source_addr));
          }
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
  static std::atomic<bool> stream_active_;
  static struct sockaddr_in video_client_addr_;
  static V4L2H264Capture *capture_;
  static Config config_;
  static SemaphoreHandle_t stream_mutex_;
  static CmdProcessor *cmd_processor_;
  static RTPPacketizer *rtp_packetizer_;
};

// Static member initialization

std::atomic<bool> UDPH264Streamer::stream_active_ = false;
const char *UDPH264Streamer::TAG = "UDP_H264";
TaskHandle_t UDPH264Streamer::data_task_handle_ = nullptr;
TaskHandle_t UDPH264Streamer::control_task_handle_ = nullptr;
bool UDPH264Streamer::is_running_ = false;
struct sockaddr_in UDPH264Streamer::video_client_addr_ = {};
V4L2H264Capture *UDPH264Streamer::capture_ = nullptr;
UDPH264Streamer::Config UDPH264Streamer::config_ = {};
CmdProcessor *UDPH264Streamer::cmd_processor_ = nullptr;
RTPPacketizer *UDPH264Streamer::rtp_packetizer_ = nullptr;
