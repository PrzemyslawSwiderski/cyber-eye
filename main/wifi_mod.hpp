#pragma once

#include <string.h>
#include <string_view>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "secrets.hpp"

enum class WiFiMode
{
  STA,
  AP
};

inline void set_str(uint8_t *dst, size_t sz, std::string_view src)
{
  snprintf(reinterpret_cast<char *>(dst), sz, "%.*s",
           static_cast<int>(src.size()), src.data());
}

class WiFiManager
{
private:
  static EventGroupHandle_t s_wifi_event_group;
  static int s_retry_num;
  static WiFiMode s_current_mode;
  static bool s_is_connected;
  static uint8_t s_ip_addr[4];
  static const char *TAG;

  static void event_handler_sta(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
  {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
      esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
      if (s_retry_num < 3)
      {
        esp_wifi_connect();
        s_retry_num++;
        ESP_LOGI(TAG, "retry to connect to the AP");
      }
      else
      {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        s_is_connected = false;
      }
      ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
      memcpy(s_ip_addr, &event->ip_info.ip.addr, 4);
      s_retry_num = 0;
      s_is_connected = true;
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
  }

  static void event_handler_ap(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
  {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
      ESP_LOGI(TAG, "station joined");
      s_is_connected = true;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
      ESP_LOGI(TAG, "station left");
      wifi_sta_list_t sta_list;
      esp_wifi_ap_get_sta_list(&sta_list);
      s_is_connected = (sta_list.num > 0);
    }
  }

  static esp_err_t init_nvs()
  {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    return ret;
  }

public:
  static const int WIFI_CONNECTED_BIT = BIT0;
  static const int WIFI_FAIL_BIT = BIT1;

  static esp_err_t init(WiFiMode mode)
  {
    ESP_ERROR_CHECK(init_nvs());

    switch (mode)
    {
    case WiFiMode::STA:
      return init_sta();
    case WiFiMode::AP:
      return init_ap(nullptr, nullptr, 4);
    default:
      return ESP_ERR_INVALID_ARG;
    }
  }

  static esp_err_t init_sta()
  {
    s_current_mode = WiFiMode::STA;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler_sta,
                                                        nullptr,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler_sta,
                                                        nullptr,
                                                        nullptr));

    wifi_config_t wifi_config = {};
    set_str(wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), CONFIG_ESP_WIFI_SSID);
    set_str(wifi_config.sta.password, sizeof(wifi_config.sta.password), CONFIG_ESP_WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
      ESP_LOGI(TAG, "connected to SSID:%s", CONFIG_ESP_WIFI_SSID);
      return ESP_OK;
    }

    ESP_LOGI(TAG, "failed to connect to SSID:%s", CONFIG_ESP_WIFI_SSID);
    return ESP_FAIL;
  }

  static esp_err_t init_ap(const char *ssid, const char *password, int max_connections)
  {
    s_current_mode = WiFiMode::AP;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler_ap,
                                                        nullptr,
                                                        nullptr));

    const char *ap_ssid = (ssid != nullptr) ? ssid : CONFIG_ESP_AP_WIFI_SSID;
    const char *ap_password = (password != nullptr) ? password : CONFIG_ESP_AP_WIFI_PASSWORD;

    wifi_config_t wifi_config = {};

    set_str(wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), ap_ssid);
    set_str(wifi_config.ap.password, sizeof(wifi_config.ap.password), ap_password);

    wifi_config.ap.max_connection = max_connections;
    wifi_config.ap.authmode = (strlen(ap_password) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif)
    {
      esp_netif_ip_info_t ip_info;
      esp_netif_get_ip_info(netif, &ip_info);
      memcpy(s_ip_addr, &ip_info.ip.addr, 4);
    }

    ESP_LOGI(TAG, "AP started. SSID:%s", ap_ssid);
    return ESP_OK;
  }

  static WiFiMode get_mode()
  {
    return s_current_mode;
  }

  static bool is_connected()
  {
    if (s_current_mode == WiFiMode::STA)
    {
      return s_is_connected;
    }
    else if (s_current_mode == WiFiMode::AP)
    {
      wifi_sta_list_t sta_list;
      return (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0);
    }
    return false;
  }

  static esp_err_t get_ip(char *ip_str, size_t len)
  {
    if (ip_str == nullptr || len < 16)
    {
      return ESP_ERR_INVALID_ARG;
    }
    snprintf(ip_str, len, "%d.%d.%d.%d", s_ip_addr[0], s_ip_addr[1], s_ip_addr[2], s_ip_addr[3]);
    return ESP_OK;
  }
};

EventGroupHandle_t WiFiManager::s_wifi_event_group = nullptr;
int WiFiManager::s_retry_num = 0;
WiFiMode WiFiManager::s_current_mode = WiFiMode::STA;
bool WiFiManager::s_is_connected = false;
uint8_t WiFiManager::s_ip_addr[4] = {0};
const char *WiFiManager::TAG = "wifi_manager";
