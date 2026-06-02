#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"

// Clear LwIP macro conflicts
#undef _IO
#undef _IOR
#undef _IOW
#undef _IOWR

#include "video_mod.hpp"
#include "rtp_packetizer_mod.hpp"
#include "cmd_process_mod.hpp"

// Forward declarations
class V4L2H264Capture;

// Define structs outside the class to avoid initialization order issues
struct UDPH264StreamerConfig
{
  // Network settings
  uint16_t data_destination_port = 59227;
  uint16_t control_port = 3334;

  // Task settings
  int stream_task_priority = 20;
  int stream_task_stack_size = 16 * 1024;
  int control_task_stack_size = 4 * 1024;
};

struct UDPH264StreamerTasks
{
  TaskHandle_t data = nullptr;
  TaskHandle_t control = nullptr;
};

class UDPH264Streamer
{
public:
  static constexpr int TOS_LOW_DELAY = 0xB8; // AF (Assured Forwarding)

  using Config = UDPH264StreamerConfig;
  using Tasks = UDPH264StreamerTasks;

  static esp_err_t start(const Config &config = Config())
  {
    if (is_running_)
    {
      ESP_LOGW(TAG, "Streamer already running");
      return ESP_ERR_INVALID_STATE;
    }

    config_ = config;
    video_client_addr_ = {};
    stream_active_ = false;

    capture_ = new V4L2H264Capture({});
    if (!capture_)
    {
      ESP_LOGE(TAG, "Failed to create capture device");
      return ESP_ERR_NO_MEM;
    }

    // Initialize components
    cmd_processor_ = std::make_unique<CmdProcessor>();
    rtp_packetizer_ = std::make_unique<RTPPacketizer>(esp_random());

    is_running_ = true;

    // Create tasks
    if (!createTasks())
    {
      cleanup();
      return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UDP streamer started");
    return ESP_OK;
  }

  static void stop()
  {
    if (!is_running_)
      return;

    is_running_ = false;

    // Give tasks time to finish
    vTaskDelay(pdMS_TO_TICKS(200));

    cleanup();
    ESP_LOGI(TAG, "Streamer stopped");
  }

private:
  static bool createTasks()
  {
    // Create control task with higher priority
    BaseType_t ret = xTaskCreatePinnedToCore(
        controlTask, "udp_control", config_.control_task_stack_size,
        nullptr, config_.stream_task_priority + 1, &tasks_.control, 1);

    if (ret != pdPASS)
    {
      ESP_LOGE(TAG, "Failed to create control task");
      return false;
    }

    // Create data task
    ret = xTaskCreatePinnedToCore(
        dataTask, "udp_stream", config_.stream_task_stack_size,
        nullptr, config_.stream_task_priority, &tasks_.data, 1);

    if (ret != pdPASS)
    {
      ESP_LOGE(TAG, "Failed to create data task");
      vTaskDelete(tasks_.control);
      tasks_.control = nullptr;
      return false;
    }

    return true;
  }

  static void cleanup()
  {
    cmd_processor_.reset();
    rtp_packetizer_.reset();
    tasks_.data = nullptr;
    tasks_.control = nullptr;
  }

  static bool initializeCapture()
  {
    if (!capture_)
      return false;

    ESP_LOGI(TAG, "Initializing video capture...");

    if (capture_->init() != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to initialize capture device");
      return false;
    }

    if (capture_->start() != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to start capture");
      capture_->stop();
      return false;
    }

    ESP_LOGI(TAG, "Video capture initialized successfully");
    return true;
  }

  static int createAndConfigureSocket(uint16_t port = 0, bool bind_socket = false)
  {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
      ESP_LOGE(TAG, "Failed to create socket: errno=%d", errno);
      return -1;
    }

    // Set TOS for low delay on data sockets
    if (!bind_socket)
    {
      setsockopt(sock, IPPROTO_IP, IP_TOS, &TOS_LOW_DELAY, sizeof(TOS_LOW_DELAY));
    }

    // Bind if requested
    if (bind_socket)
    {
      struct sockaddr_in addr = {};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_port = htons(port);

      if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
      {
        ESP_LOGE(TAG, "Failed to bind socket to port %d", port);
        close(sock);
        return -1;
      }
    }

    return sock;
  }

  static void sendFrame(int sock, const uint8_t *data, size_t size, const struct sockaddr_in &dest)
  {
    if (!rtp_packetizer_)
      return;

    uint64_t ts_us = esp_timer_get_time();
    auto packets = rtp_packetizer_->packetize(data, size, ts_us);

    for (size_t i = 0; i < packets.size(); i++)
    {
      if (!sendPacket(sock, packets[i], dest, i, packets.size()))
        return;
    }
  }

  static bool sendPacket(int sock, const std::vector<uint8_t> &packet,
                         const struct sockaddr_in &dest, size_t index, size_t total)
  {
    int sent = sendto(sock, packet.data(), packet.size(), 0,
                      (const struct sockaddr *)&dest, sizeof(dest));

    if (sent > 0)
      return true;

    if (errno == ENOMEM || errno == ENOBUFS)
    {
      ESP_LOGW(TAG, "Send buffer full, dropping packet %zu/%zu", index + 1, total);
      taskYIELD();
    }
    else
    {
      ESP_LOGE(TAG, "Send error (packet %zu/%zu): errno=%d", index + 1, total, errno);
      return false;
    }

    return false;
  }

  static void dataTask(void *pvParameters)
  {
    if (!capture_ || !rtp_packetizer_)
    {
      vTaskDelete(NULL);
      return;
    }

    int sock = createAndConfigureSocket();
    if (sock < 0)
    {
      vTaskDelete(NULL);
      return;
    }

    // Initialize capture at start
    if (!initializeCapture())
    {
      ESP_LOGE(TAG, "Failed to start video capture");
      close(sock);
      vTaskDelete(NULL);
      return;
    }

    rtp_packetizer_->resetSequence();
    ESP_LOGI(TAG, "Data task started");

    // FPS tracking variables
    uint32_t frame_count = 0;
    TickType_t last_time = xTaskGetTickCount();

    while (is_running_)
    {
      if (!stream_active_)
      {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      uint8_t *frame_data = nullptr;
      size_t frame_size = 0;
      uint32_t sequence;

      if (capture_->captureFrame(frame_data, frame_size, sequence))
      {
        sendFrame(sock, frame_data, frame_size, video_client_addr_);
        frame_count++;
      }
      else
      {
        ESP_LOGW(TAG, "Capture failed");
        delete capture_;
        capture_ = new V4L2H264Capture({});
        initializeCapture();
        vTaskDelay(pdMS_TO_TICKS(10));
      }

      // Log FPS every second
      TickType_t now = xTaskGetTickCount();
      if ((now - last_time) >= pdMS_TO_TICKS(1000))
      {
        ESP_LOGI(TAG, "FPS: %lu", frame_count);
        frame_count = 0;
        last_time = now;
      }
    }

    ESP_LOGI(TAG, "Data task closing");
    close(sock);
    vTaskDelete(NULL);
  }

  static void controlTask(void *pvParameters)
  {
    int sock = createAndConfigureSocket(config_.control_port, true);
    if (sock < 0)
    {
      vTaskDelete(NULL);
      return;
    }

    ESP_LOGI(TAG, "Control server bound to port %d", config_.control_port);

    char buffer[256];
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);

    while (is_running_)
    {
      int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&source_addr, &addr_len);

      if (len > 0)
      {
        buffer[len] = '\0';
        processCommand(sock, buffer, source_addr);
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }

    close(sock);
    vTaskDelete(NULL);
  }

  static void processCommand(int sock, const char *command, struct sockaddr_in &source_addr)
  {
    char source_ip[16];
    inet_ntoa_r(source_addr.sin_addr, source_ip, sizeof(source_ip));
    ESP_LOGI(TAG, "Command \"%s\" from %s:%d", command, source_ip, ntohs(source_addr.sin_port));

    if (!cmd_processor_)
      return;

    CmdProcessor::Context ctx;
    ctx.stream_active = &stream_active_;
    ctx.video_client_addr = &video_client_addr_;
    ctx.source_addr = &source_addr;
    ctx.capture = capture_;

    auto result = cmd_processor_->process(command, ctx);

    if (result.response)
    {
      sendto(sock, result.response, strlen(result.response), 0,
             (const struct sockaddr *)&source_addr, sizeof(source_addr));
    }
  }

  // Static members
  static constexpr const char *TAG = "UDP_H264";
  static inline bool is_running_ = false;
  static inline std::atomic<bool> stream_active_ = false;
  static inline V4L2H264Capture *capture_ = nullptr;
  static inline Config config_;
  static inline struct sockaddr_in video_client_addr_{};
  static inline Tasks tasks_;
  static inline std::unique_ptr<CmdProcessor> cmd_processor_;
  static inline std::unique_ptr<RTPPacketizer> rtp_packetizer_;
};
