#pragma once

#include "secrets.hpp"
#include "logger.hpp"
#include "wifi_sta.hpp"
#include "wifi_ap.hpp"

#include <string>

namespace wifi
{
  // ── Config ────────────────────────────────────────────────────────────────

  enum class WifiMode : uint8_t
  {
    STA = 0,
    AP = 1
  };

  struct WifiConfig
  {
    WifiMode mode = WifiMode::STA;
    std::string ssid = {};
    std::string password = {};
    uint8_t channel = 1; // AP only
  };

  // ── State ─────────────────────────────────────────────────────────────────

  inline std::unique_ptr<espp::WifiSta> wifi_sta;
  inline std::unique_ptr<espp::WifiAp> wifi_ap;
  inline espp::Logger logger({.tag = "WIFI", .level = espp::Logger::Verbosity::INFO});

  // ── Internal ──────────────────────────────────────────────────────────────

  namespace detail
  {
    inline void apply(const WifiConfig &cfg)
    {
      wifi_sta.reset();
      wifi_ap.reset();

      esp_wifi_set_mode(WIFI_MODE_NAN);
      if (cfg.mode == WifiMode::STA)
      {
        logger.info("Starting STA (ssid={})", cfg.ssid);

        wifi_sta = std::make_unique<espp::WifiSta>(espp::WifiSta::Config{
            .ssid = cfg.ssid,
            .password = cfg.password,
            .num_connect_retries = 3,
            .on_connected = []()
            { logger.info("WiFi connected"); },
            .on_disconnected = []()
            { logger.warn("WiFi disconnected"); },
            .on_got_ip = [](ip_event_got_ip_t *ev)
            {
              char ip[16];
              snprintf(ip, sizeof(ip), "%d.%d.%d.%d", IP2STR(&ev->ip_info.ip));
              logger.info("Got IP: {}", ip); },
            .log_level = espp::Logger::Verbosity::DEBUG});
      }
      else
      {
        logger.info("Starting AP (ssid={}, ch={})", cfg.ssid, cfg.channel);
        wifi_ap = std::make_unique<espp::WifiAp>(espp::WifiAp::Config{
            .ssid = cfg.ssid,
            .password = cfg.password,
            .phy_rate = WIFI_PHY_RATE_MCS9_LGI,
            .channel = cfg.channel,
            .log_level = espp::Logger::Verbosity::DEBUG});
      }
    }
  } // namespace detail

  // ── Public API ────────────────────────────────────────────────────────────

  inline void start_wifi_task()
  {
    if (wifi_sta || wifi_ap)
    {
      logger.warn("WiFi already running – call stop_wifi_task() first");
      return;
    }
    detail::apply(WifiConfig{
        .mode = WifiMode::AP,
        .ssid = CONFIG_ESP_AP_WIFI_SSID,
        .password = CONFIG_ESP_AP_WIFI_PASSWORD,
    });
  }

  inline void stop_wifi_task()
  {
    if (!wifi_sta && !wifi_ap)
    {
      logger.warn("WiFi not running");
      return;
    }
    wifi_sta.reset();
    wifi_ap.reset();
    logger.info("WiFi stopped");
  }

  inline void update_wifi_config(const WifiConfig &cfg)
  {
    logger.info("Updating config (mode={}, ssid={})",
                cfg.mode == WifiMode::STA ? "STA" : "AP", cfg.ssid);
    detail::apply(cfg);
  }

  // ── Utility ───────────────────────────────────────────────────────────────

  inline int8_t get_signal_strength()
  {
    // if (!wifi_sta)
    // {
    //   logger.warn("Not in STA mode");
    //   return -127;
    // }
    wifi_ap_record_t rec;
    if (esp_wifi_sta_get_ap_info(&rec) != ESP_OK)
    {
      logger.error("Failed to read RSSI");
      return -127;
    }
    return rec.rssi;
  }

  inline bool is_connected()
  {
    return wifi_sta && wifi_sta->is_connected();
  }

  inline std::string get_ip()
  {
    if (wifi_sta)
      return wifi_sta->get_ip_address();
    if (wifi_ap)
      return wifi_ap->get_ip_address();
    return {};
  }

} // namespace wifi
