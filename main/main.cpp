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

  // Configure video capture
  V4L2H264Capture::Config capture_config;
  capture_config.capture_device = "/dev/video0";

  // I-frame Interval (default: 12)
  capture_config.i_period = 30;
  // 0 to 51 (where 0 is near-perfect/lossless quality and 51 is the worst quality)
  capture_config.quality = 46;
  // Exposure (default: 90)
  capture_config.exposure = 80;

  // Initialize capture (but don't start streaming yet)
  V4L2H264Capture capture(capture_config);

  // Configure UDP streamer
  UDPH264Streamer::Config streamer_config = {};

  // Start streamer (waits for client commands)
  if (UDPH264Streamer::start(&capture, streamer_config) != ESP_OK)
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

  esp_codec_dev_handle_t out_handle = NULL;
  dev_audio_codec_handles_t *play_dev_handle = NULL;

  esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&play_dev_handle);
  if (play_dev_handle == NULL || play_dev_handle->codec_dev == NULL)
  {
    ESP_LOGE(TAG, "Failed to get playback handle");
    return;
  }

  out_handle = play_dev_handle->codec_dev; // Direct assignment, no dereferencing

  esp_err_t ret = esp_codec_dev_set_out_vol(out_handle, 10); // Pass the handle directly
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to set output volume");
    return;
  }

  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = 48000;
  fs.channel = 2;
  fs.bits_per_sample = 16;

  ret = esp_codec_dev_open(out_handle, &fs); // Pass the handle directly
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to open playback codec");
    return;
  }

  MusicPlayerMod player;

  // Initialize with your playback device
  if (player.init(out_handle) != ESP_OK) // Pass the handle directly, not a pointer to it
  {
    ESP_LOGE("APP", "Failed to initialize music player");
    return;
  }

  // Play MP3 file from SD card
  player.play("file://sdcard/test.mp3");

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
