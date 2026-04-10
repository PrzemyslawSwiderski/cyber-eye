#pragma once

#include "secrets.hpp"
#include "logger.hpp"

#include <string>
#include <string_view>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

namespace wifi
{
  // ── types ──────────────────────────────────────────────────────────────────
  enum class Mode : uint8_t
  {
    STA = WIFI_MODE_STA,
    AP = WIFI_MODE_AP,
    NULL_ = WIFI_MODE_NULL,
  };

  struct StaCredentials
  {
    std::string_view ssid;
    std::string_view password;
  };

  // ── constants ──────────────────────────────────────────────────────────────
  inline constexpr int MAX_RETRY = 3;
  inline constexpr EventBits_t CONNECTED_BIT = BIT0;
  inline constexpr EventBits_t FAIL_BIT = BIT1;

  // ── state ──────────────────────────────────────────────────────────────────
  inline espp::Logger logger({.tag = "WIFI", .level = espp::Logger::Verbosity::INFO});
  inline EventGroupHandle_t event_group = nullptr;
  inline esp_netif_t *sta_netif = nullptr;
  inline esp_netif_t *ap_netif = nullptr;
  inline esp_event_handler_instance_t wifi_handler = nullptr;
  inline esp_event_handler_instance_t ip_handler = nullptr;
  inline std::atomic<bool> connected{false};
  inline int retry_num = 0;

  // ── utilities ──────────────────────────────────────────────────────────────
  inline bool set_mode(Mode mode, StaCredentials credentials = {});
  
  inline void set_str(uint8_t *dst, size_t sz, std::string_view src)
  {
    snprintf(reinterpret_cast<char *>(dst), sz, "%.*s",
             static_cast<int>(src.size()), src.data());
  }

  inline Mode get_current_mode()
  {
    wifi_mode_t m = WIFI_MODE_NULL;
    esp_wifi_get_mode(&m);
    return static_cast<Mode>(m);
  }

  // ── configuration ──────────────────────────────────────────────────────────
  inline void configure_sta(StaCredentials credentials)
  {
    wifi_config_t wifi_config{};
    set_str(wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), credentials.ssid);
    set_str(wifi_config.sta.password, sizeof(wifi_config.sta.password), credentials.password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  }

  inline void configure_ap()
  {
    wifi_config_t wifi_config{};
    set_str(wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), CONFIG_ESP_AP_WIFI_SSID);
    set_str(wifi_config.ap.password, sizeof(wifi_config.ap.password), CONFIG_ESP_AP_WIFI_PASSWORD);
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.max_connection = 3;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  }

  // ── STA event handlers ─────────────────────────────────────────────────────
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
      logger.warn("Max retries reached, falling back to AP mode");
      set_mode(Mode::AP);
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

  // ── AP event handlers ──────────────────────────────────────────────────────
  inline void on_ap_start(void *)
  {
    connected = true;
    xEventGroupSetBits(event_group, CONNECTED_BIT);
    logger.info("AP started, SSID: {}", CONFIG_ESP_AP_WIFI_SSID);
  }

  inline void on_ap_stop(void *)
  {
    connected = false;
    logger.info("AP stopped");
  }

  inline void on_ap_sta_connected(void *data)
  {
    auto *event = static_cast<wifi_event_ap_staconnected_t *>(data);
    logger.info("Client connected,    MAC: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}, AID: {}",
                event->mac[0], event->mac[1], event->mac[2],
                event->mac[3], event->mac[4], event->mac[5],
                event->aid);
  }

  inline void on_ap_sta_disconnected(void *data)
  {
    auto *event = static_cast<wifi_event_ap_stadisconnected_t *>(data);
    logger.info("Client disconnected, MAC: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}, AID: {}",
                event->mac[0], event->mac[1], event->mac[2],
                event->mac[3], event->mac[4], event->mac[5],
                event->aid);
  }

  // ── event dispatchers ──────────────────────────────────────────────────────
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
    case WIFI_EVENT_AP_START:
      return on_ap_start(data);
    case WIFI_EVENT_AP_STOP:
      return on_ap_stop(data);
    case WIFI_EVENT_AP_STACONNECTED:
      return on_ap_sta_connected(data);
    case WIFI_EVENT_AP_STADISCONNECTED:
      return on_ap_sta_disconnected(data);
    default:
      break;
    }
  }

  inline void ip_event_handler(void *, esp_event_base_t, int32_t id, void *data)
  {
    if (id == IP_EVENT_STA_GOT_IP)
      on_got_ip(data);
  }

  // ── lifecycle ──────────────────────────────────────────────────────────────
  inline void register_handlers()
  {
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, nullptr, &wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &ip_event_handler, nullptr, &ip_handler));
  }

  inline void unregister_handlers()
  {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
    wifi_handler = nullptr;
    ip_handler = nullptr;
  }

  inline void teardown()
  {
    retry_num = MAX_RETRY; // prevent retry handler from reconnecting during teardown

    if (get_current_mode() == Mode::STA)
      ESP_ERROR_CHECK(esp_wifi_disconnect());

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    unregister_handlers();

    if (sta_netif)
    {
      esp_netif_destroy(sta_netif);
      sta_netif = nullptr;
    }
    if (ap_netif)
    {
      esp_netif_destroy(ap_netif);
      ap_netif = nullptr;
    }

    vEventGroupDelete(event_group);
    event_group = nullptr;
    connected = false;
    retry_num = 0;
  }

  inline bool wait_for_connection()
  {
    EventBits_t bits = xEventGroupWaitBits(
        event_group,
        CONNECTED_BIT | FAIL_BIT,
        pdFALSE, pdFALSE,
        portMAX_DELAY);

    if (bits & CONNECTED_BIT)
    {
      logger.info("WiFi ready");
      return true;
    }

    logger.warn("WiFi failed to connect");
    return false;
  }

  inline void init()
  {
    event_group = xEventGroupCreate();
    retry_num = 0;
    connected = false;

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    register_handlers();
  }

  inline void start()
  {
    ESP_ERROR_CHECK(esp_wifi_start());
  }

  // ── public API ─────────────────────────────────────────────────────────────
  //
  // Prerequisites in app_main() before calling begin():
  //   nvs_flash_init()  (or erase + reinit)
  //   esp_netif_init()
  //   esp_event_loop_create_default()

  inline void begin()
  {
    logger.info("Starting WiFi from saved flash config");
    init();

    // esp_wifi_start() will restore mode+credentials from flash
    start();

    if (get_current_mode() == Mode::NULL_)
    {
      logger.warn("No saved WiFi config, falling back to AP mode");
      configure_ap();
      start();
    }

    wait_for_connection();
  }

  inline bool set_mode(Mode mode, StaCredentials credentials)
  {
    if (sta_netif || ap_netif)
      teardown();

    init(); // init driver without starting

    if (mode == Mode::STA)
    {
      logger.info("Starting in STA mode, SSID: {}", credentials.ssid);
      configure_sta(credentials);
    }
    else
    {
      logger.info("Starting in AP mode");
      configure_ap();
    }

    start(); // start only after mode is configured
    return wait_for_connection();
  }

  inline void stop()
  {
    if (!sta_netif && !ap_netif)
    {
      logger.warn("WiFi not running");
      return;
    }
    logger.info("Stopping WiFi...");
    teardown();
    logger.info("WiFi stopped");
  }

  inline bool is_connected() { return connected.load(); }

  inline int8_t get_signal_strength()
  {
    if (get_current_mode() != Mode::STA || !sta_netif)
    {
      logger.warn("RSSI only available in STA mode");
      return -127;
    }

    wifi_ap_record_t rec{};
    if (esp_err_t err = esp_wifi_sta_get_ap_info(&rec); err != ESP_OK)
    {
      logger.error("Failed to get RSSI: {}", esp_err_to_name(err));
      return -127;
    }
    return rec.rssi;
  }

  inline std::string get_ip()
  {
    esp_netif_t *netif = (get_current_mode() == Mode::STA)
                             ? sta_netif
                             : ap_netif;

    if (!netif || !connected)
      return {};

    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK)
      return {};

    char buf[16];
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
    return buf;
  }

} // namespace wifi
