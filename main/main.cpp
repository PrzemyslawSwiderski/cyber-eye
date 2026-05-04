#include "esp_log.h"
#include "esp_board_manager_includes.h"

#include "wifi_mod.hpp"
#include "udp_server_mod.hpp"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
  esp_log_level_set("DEV_FS_FAT_SUB_SDMMC", ESP_LOG_DEBUG);
  esp_log_level_set("esp_video_init", ESP_LOG_DEBUG);
  esp_log_level_set("lwip", ESP_LOG_DEBUG);

  ESP_LOGI(TAG, "Starting Cyber Eye...");

  // Initialize board manager, which will automatically initialize all peripherals and devices
  ESP_LOGI(TAG, "Initializing board manager...");
  int ret = esp_board_manager_init();
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize board manager");
    return;
  }
  esp_board_manager_print_board_info();
  esp_board_manager_print();

  WiFiMode mode = WiFiMode::STA;

  if (WiFiManager::init(mode) == ESP_OK)
  {
    char ip_str[16];
    WiFiManager::get_ip(ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "WiFi ready! IP: %s", ip_str);

    // Configure video capture
    V4L2H264Capture::Config capture_config;
    capture_config.capture_device = "/dev/video0";
    // capture_config.bitrate = 2000000; // 2 Mbps
    capture_config.bitrate = 1000000; // 1 Mbps
    // capture_config.bitrate = 25000;
    // capture_config.bitrate = 25000000;

    // I-frame Interval (default: 12)
    // capture_config.i_period = 30;
    capture_config.i_period = 15;
    // 0 to 51 (where 0 is near-perfect/lossless quality and 51 is the worst quality)
    capture_config.min_qp = 35;
    capture_config.max_qp = 35;

    // Initialize capture (but don't start streaming yet)
    V4L2H264Capture capture(capture_config);

    // Configure UDP streamer
    UDPH264Streamer::Config streamer_config;
    streamer_config.data_destination_port = 3333;
    streamer_config.control_port = 3334;

    // Start streamer (waits for client commands)
    if (UDPH264Streamer::start(&capture, streamer_config) != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to start streamer");
      return;
    }

    ESP_LOGI(TAG, "System ready. Available commands:");
    ESP_LOGI(TAG, "  Send 'start' to port %d to start streaming", streamer_config.control_port);
    ESP_LOGI(TAG, "  Send 'stop' to port %d to stop streaming", streamer_config.control_port);
    ESP_LOGI(TAG, "  Send 'status' to port %d to check status", streamer_config.control_port);

    ESP_LOGI(TAG, "Server ready");
    ESP_LOGI(TAG, "POST JSON to http://%s/command", ip_str);
  }

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
