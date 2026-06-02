#include "esp_log.h"
#include "esp_board_manager_includes.h"

#include "wifi_mod.hpp"
#include "udp_server_mod.hpp"
#include "tasks_mod.hpp"
#include "file_server_mod.hpp"
#include "music_mod.hpp"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
  esp_log_level_set("DEV_FS_FAT_SUB_SDMMC", ESP_LOG_DEBUG);
  // esp_log_level_set("SD_HOST", ESP_LOG_DEBUG);
  esp_log_level_set("esp_video_init", ESP_LOG_DEBUG);
  esp_log_level_set("lwip", ESP_LOG_DEBUG);

  ESP_LOGI(TAG, "Starting Cyber Eye...");

  // Initialize network interface
  ESP_ERROR_CHECK(esp_netif_init());
  // Create default event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  // Initialize board manager, which will automatically initialize all peripherals and devices
  ESP_ERROR_CHECK(esp_board_manager_init());

  esp_board_manager_print_board_info();
  esp_board_manager_print();

  // Initialize WiFi first
  wifi::begin();

  while (!wifi::is_connected())
  {
    // waiting for the WIFI connection
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  auto ip = wifi::get_ip();

  // Configure UDP streamer
  UDPH264Streamer::Config streamer_config = {};

  // Start streamer (waits for client commands)
  if (UDPH264Streamer::start(streamer_config) != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to start streamer");
    return;
  }

  // In your app_main or initialization:
  HttpFileServer file_server("/sdcard");

  // Start server on port 8080
  if (file_server.start(8080) == ESP_OK)
  {
    ESP_LOGI("MAIN", "File server running at http://[IP]:8080");
  }
  else
  {
    ESP_LOGE("MAIN", "Failed to start file server");
  };

  // // Let it play for 10 seconds
  // vTaskDelay(pdMS_TO_TICKS(10000));

  // // Check state
  // esp_asp_state_t state = player.get_state();
  // ESP_LOGI("APP", "Current state: %d", state);

  // // Stop playback
  // player.stop();

  // // Cleanup
  // player.deinit();

  ESP_LOGI(TAG, "Cyber Eye ready");

  while (true)
  {
    // auto tasks_output = tasks::to_table();
    // ESP_LOGI(TAG, "\n%s", tasks_output.c_str());
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
