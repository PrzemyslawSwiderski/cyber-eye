#include "wifi_mod.hpp"
#include "esp_log.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
  // Choose mode: WiFiMode::STA or WiFiMode::AP
  WiFiMode mode = WiFiMode::AP;
  // WiFiMode mode = WiFiMode::STA;

  ESP_LOGI(TAG, "Starting WiFi...");

  esp_err_t ret = WiFiManager::init(mode);

  if (ret == ESP_OK)
  {
    char ip_str[16];
    WiFiManager::get_ip(ip_str, sizeof(ip_str));

    if (mode == WiFiMode::STA)
    {
      ESP_LOGI(TAG, "Connected! IP: %s", ip_str);
    }
    else
    {
      ESP_LOGI(TAG, "AP started! IP: %s", ip_str);
      ESP_LOGI(TAG, "Connect to SSID: %s", CONFIG_ESP_WIFI_SSID);
    }
  }
  else
  {
    ESP_LOGE(TAG, "WiFi initialization failed");
  }

  while (true)
  {
    if (WiFiManager::is_connected())
    {
      ESP_LOGI(TAG, "WiFi connected");
    }
    else
    {
      ESP_LOGI(TAG, "WiFi disconnected");
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
