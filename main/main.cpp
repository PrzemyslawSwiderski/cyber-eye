#include <chrono>
#include "logger.hpp"

#include "secrets.hpp"
#include "ftp_mod.hpp"
#include "wifi_mod.hpp"
#include "music_player.hpp"
#include "http_controller.hpp"
#include "file_system.hpp"

#include "esp_board_manager.h"
#include "esp_gmf_app_cli.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_sys.h"
#include "esp_littlefs.h"
#include "esp_netif.h"
#include "nvs_flash.h"

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

  // NVS must come first — wifi::begin() reads from it
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize network interface
  ESP_ERROR_CHECK(esp_netif_init());
  // Create default event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_board_manager_init();
  esp_gmf_app_cli_init("cyber-eye> ", NULL);

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

  auto signal_str = wifi::get_signal_strength();
  logger.info("WiFi signal strength: {} dBm (-30 -> excellent, -90 -> weak)", signal_str);

  // Start FTP server
  ftp::start_server_task(ip);

  ctrl::HttpController::Config http_config;
  http_config.port = 8080;
  http_config.bind_address = ip;

  g_http_controller = std::make_unique<ctrl::HttpController>(player.get(), http_config);
  g_http_controller->start_task();

  // wifi::StaCredentials creds({.ssid = CONFIG_ESP_WIFI_SSID,
  //                             .password = CONFIG_ESP_WIFI_PASSWORD});
  // wifi::set_mode(wifi::Mode::STA, creds);
  
  // wifi::set_mode(wifi::Mode::AP, {});

  while (1)
  {
    // infinite main loop
    // vTaskDelay(pdMS_TO_TICKS(100));
    std::this_thread::sleep_for(100ms);
  }
}
