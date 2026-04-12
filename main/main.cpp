#include <chrono>
#include "logger.hpp"

#include "secrets.hpp"
#include "ftp_mod.hpp"
#include "wifi_mod.hpp"
#include "music_player.hpp"
#include "http_controller.hpp"
#include "file_system.hpp"
#include "tasks_mod.hpp"
#include "video_recorder.hpp"

#include "esp_board_manager.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_sys.h"
#include "esp_littlefs.h"
#include "esp_netif.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"

using namespace std::chrono_literals;

// Global controller to keep it alive
static std::unique_ptr<ctrl::HttpController> g_http_controller;

extern "C" void app_main()
{
  espp::Logger logger({.tag = "MAIN", .level = espp::Logger::Verbosity::DEBUG});
  logger.info("Bootup");

  // Set the log level for the "SDIO_SLAVE" tag to VERBOSE
  esp_log_level_set("SDIO_SLAVE", ESP_LOG_VERBOSE);
  // esp_log_level_set("H_SDIO_DRV", ESP_LOG_VERBOSE);

  // Initialize network interface
  ESP_ERROR_CHECK(esp_netif_init());
  // Create default event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_ERROR_CHECK(esp_board_manager_init());

  // Create and initialize player
  auto player = std::make_unique<audio::MusicPlayer>();
  player->initialize();

  // Initialize WiFi first
  wifi::begin();

  while (!wifi::is_connected())
  {
    // waiting for the WIFI connection
    std::this_thread::sleep_for(50ms);
  }

  auto ip = wifi::get_ip();

  // Start FTP server
  ftp::start_server_task(ip);

  ctrl::HttpController::Config http_config;
  http_config.port = 8080;
  http_config.bind_address = ip;

  g_http_controller = std::make_unique<ctrl::HttpController>(player.get(), http_config);
  g_http_controller->start_task();

  // ctrl::VideoRecorder recorder;
  // if (recorder.init() != ESP_OK)
  // {
  //   logger.error("Recorder init failed");
  //   return;
  // }

  while (1)
  {
    // Log table to console
    // Run for 6 seconds, logging FPS every 3 s, then print summary and exit
    // recorder.start_benchmark(6);
    // logger.info("Task list:\n{}", tasks::to_table());
    std::this_thread::sleep_for(5s);
  }
}
