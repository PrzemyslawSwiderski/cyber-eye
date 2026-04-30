#include "wifi_mod.hpp"
#include "udp_server_mod.hpp"
#include "http_controller_mod.hpp"
#include "esp_log.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
  WiFiMode mode = WiFiMode::STA;

  ESP_LOGI(TAG, "Starting ESP32 Streaming Server...");

  if (WiFiManager::init(mode) == ESP_OK)
  {
    char ip_str[16];
    WiFiManager::get_ip(ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "WiFi ready! IP: %s", ip_str);

    UDPServer::start(3333);

    HTTPController::set_callbacks(
        UDPServer::start_stream,
        UDPServer::stop_stream,
        UDPServer::is_streaming,
        UDPServer::get_frame_count);

    HTTPController::start(80);

    ESP_LOGI(TAG, "Server ready");
    ESP_LOGI(TAG, "POST JSON to http://%s/command", ip_str);
  }

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
