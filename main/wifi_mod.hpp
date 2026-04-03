#pragma once

#include "secrets.hpp"
#include "logger.hpp"

#include <string>
#include <atomic>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

namespace wifi
{
  namespace detail
  {
    constexpr int MAX_RETRY = 3;
    constexpr EventBits_t CONNECTED_BIT = BIT0;
    constexpr EventBits_t FAIL_BIT = BIT1;

    inline espp::Logger logger({.tag = "WIFI", .level = espp::Logger::Verbosity::INFO});
    inline EventGroupHandle_t event_group = nullptr;
    inline esp_netif_t *sta_netif = nullptr;
    inline esp_event_handler_instance_t wifi_handler = nullptr;
    inline esp_event_handler_instance_t ip_handler = nullptr;
    inline std::atomic<bool> connected{false};
    inline int retry_num = 0;

    inline void on_sta_start(void *)
    {
      esp_wifi_connect();
    }

    inline void on_sta_connected(void *)
    {
      retry_num = 0;
      connected = true;
      logger.info("WiFi connected");
    }

    inline void on_sta_disconnected(void *)
    {
      connected = false;
      logger.warn("WiFi disconnected");

      if (retry_num < MAX_RETRY)
      {
        ++retry_num;
        logger.info("Retrying connection ({}/{})", retry_num, MAX_RETRY);
        esp_wifi_connect();
      }
      else
      {
        logger.warn("Max retries reached, giving up");
        xEventGroupSetBits(event_group, FAIL_BIT);
      }
    }

    inline void on_got_ip(void *data)
    {
      auto *event = static_cast<ip_event_got_ip_t *>(data);
      xEventGroupSetBits(event_group, CONNECTED_BIT);

      char buf[16];
      snprintf(buf, sizeof(buf), IPSTR, IP2STR(&event->ip_info.ip));
      logger.info("Got IP: {}", buf);
    }

    inline void wifi_event_handler(void *, esp_event_base_t, int32_t id, void *data)
    {
      switch (id)
      {
      case WIFI_EVENT_STA_START:
        return on_sta_start(data);
      case WIFI_EVENT_STA_CONNECTED:
        return on_sta_connected(data);
      case WIFI_EVENT_STA_DISCONNECTED:
        return on_sta_disconnected(data);
      default:
        break;
      }
    }

    inline void ip_event_handler(void *, esp_event_base_t, int32_t id, void *data)
    {
      if (id == IP_EVENT_STA_GOT_IP)
        on_got_ip(data);
    }

    inline void set_str(uint8_t *dst, size_t sz, const char *src)
    {
      snprintf(reinterpret_cast<char *>(dst), sz, "%s", src);
    }
  }

  // Prerequisite: esp_netif_init() and esp_event_loop_create_default()
  // must be called in app_main() before start_wifi_task().
  inline void start_wifi_task()
  {
    if (detail::sta_netif)
    {
      detail::logger.warn("WiFi already running");
      return;
    }

    detail::logger.info("Starting WiFi...");
    detail::event_group = xEventGroupCreate();
    detail::retry_num = 0;
    detail::connected = false;
    detail::sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &detail::wifi_event_handler, nullptr, &detail::wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &detail::ip_event_handler, nullptr, &detail::ip_handler));

    wifi_config_t wifi_config{};
    detail::set_str(wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), CONFIG_ESP_WIFI_SSID);
    detail::set_str(wifi_config.sta.password, sizeof(wifi_config.sta.password), CONFIG_ESP_WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        detail::event_group,
        detail::CONNECTED_BIT | detail::FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY);

    if (bits & detail::CONNECTED_BIT)
      detail::logger.info("WiFi init complete");
    else if (bits & detail::FAIL_BIT)
      detail::logger.warn("Failed to connect to SSID: {}", CONFIG_ESP_WIFI_SSID);
    else
      detail::logger.error("Unexpected event during WiFi init");
  }

  inline void stop_wifi_task()
  {
    if (!detail::sta_netif)
    {
      detail::logger.warn("WiFi not running");
      return;
    }

    detail::logger.info("Stopping WiFi...");

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, detail::wifi_handler);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, detail::ip_handler);
    detail::wifi_handler = nullptr;
    detail::ip_handler = nullptr;

    esp_netif_destroy(detail::sta_netif);
    detail::sta_netif = nullptr;

    vEventGroupDelete(detail::event_group);
    detail::event_group = nullptr;

    detail::connected = false;
    detail::logger.info("WiFi stopped");
  }

  inline int8_t get_signal_strength()
  {
    if (!detail::sta_netif)
    {
      detail::logger.warn("WiFi not running");
      return -127;
    }

    wifi_ap_record_t rec{};
    if (esp_err_t err = esp_wifi_sta_get_ap_info(&rec); err != ESP_OK)
    {
      detail::logger.error("Failed to get RSSI: {}", esp_err_to_name(err));
      return -127;
    }
    return rec.rssi;
  }

  inline bool is_connected()
  {
    return detail::connected.load();
  }

  inline std::string get_ip()
  {
    if (!detail::sta_netif || !detail::connected)
      return {};

    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(detail::sta_netif, &info) != ESP_OK)
      return {};

    char buf[16];
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
    return buf;
  }

}
