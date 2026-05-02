#include "esp_log.h"
#include "esp_board_manager_includes.h"

#include "wifi_mod.hpp"
#include "udp_server_mod.hpp"

// Clear the LwIP definitions of these macros
#undef _IO
#undef _IOR
#undef _IOW
#undef _IOWR
#include "video_mod.hpp"

static const char *TAG = "main";

extern "C" void app_main(void)
{
  esp_log_level_set("DEV_FS_FAT_SUB_SDMMC", ESP_LOG_DEBUG);
  esp_log_level_set("esp_video_init", ESP_LOG_DEBUG);

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

    UDPServer::start(3333);

    ESP_LOGI(TAG, "Server ready");
    ESP_LOGI(TAG, "POST JSON to http://%s/command", ip_str);
  }
  
  V4L2H264Capture::Config config;
  config.capture_device = "/dev/video0";
  config.width = 1280;
  config.height = 960;
  config.bitrate = 4000000; // 4 Mbps
  config.i_period = 30;
  config.verbose = true;

  V4L2H264Capture capture(config);

  if (capture.init() != ESP_OK)
  {
    ESP_LOGE("MAIN", "Failed to init capture");
    return;
  }

  if (capture.start() != ESP_OK)
  {
    ESP_LOGE("MAIN", "Failed to start capture");
    return;
  }

  // Capture frames
  for (int i = 0; i < 100000; i++)
  {
    size_t frame_size = 0;
    uint8_t *frame_data = capture.captureFrame(frame_size);

    if (frame_data && frame_size > 0)
    {
      ESP_LOGI("MAIN", "Frame %d: %zu bytes", i, frame_size);
      // Process frame_data here...
    }
  }

  capture.stop();

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
