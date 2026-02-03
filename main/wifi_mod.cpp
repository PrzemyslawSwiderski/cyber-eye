#include "wifi_mod.hpp"
#include "secrets.hpp"
#include "logger.hpp"
#include "wifi_sta.hpp"
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>

using namespace std::chrono_literals;

namespace wifi
{
  static std::unique_ptr<espp::WifiSta> wifi_sta;
  static espp::Logger logger({.tag = "WIFI", .level = espp::Logger::Verbosity::INFO});

  void start_wifi_task()
  {
    if (wifi_sta)
    {
      logger.warn("WiFi already running");
      return;
    }

    logger.info("Starting WiFi...");

    auto wifi_config = espp::WifiSta::Config{
        .ssid = CONFIG_ESP_WIFI_SSID,
        .password = CONFIG_ESP_WIFI_PASSWORD,
        .phy_rate = WIFI_PHY_RATE_MCS9_LGI,
        .num_connect_retries = 3,
        .on_connected = []()
        { logger.info("WiFi connected"); },
        .on_disconnected = []()
        { logger.warn("WiFi disconnected"); },
        .on_got_ip = [](ip_event_got_ip_t *eventdata)
        { 
          char ip_str[16];
          snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", 
                   IP2STR(&eventdata->ip_info.ip));
          
          logger.info("Got IP: {}", ip_str); },
        .log_level = espp::Logger::Verbosity::DEBUG};

    wifi_sta = std::make_unique<espp::WifiSta>(wifi_config);

    logger.info("WiFi init complete!");
  }

  void stop_wifi_task()
  {
    if (wifi_sta)
    {
      logger.info("Stopping WiFi...");
      wifi_sta.reset();

      logger.info("WiFi stopped");
    }
    else
    {
      logger.warn("WiFi not running");
    }
  }

  int8_t get_signal_strength()
  {
    if (!wifi_sta)
    {
      logger.warn("WiFi not running");
      return -127; // Return minimum RSSI value
    }

    wifi_ap_record_t ap_record;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_record);

    if (err != ESP_OK)
    {
      logger.error("Failed to get WiFi signal strength: {}", esp_err_to_name(err));
      return -127;
    }
    return ap_record.rssi;
  }

  bool is_connected()
  {
    return wifi_sta->is_connected();
  }

  std::string get_ip()
  {
    return wifi_sta->get_ip_address();
  }
}
