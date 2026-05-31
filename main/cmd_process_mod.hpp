#pragma once

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"
#include "wifi_mod.hpp"
#include "video_mod.hpp"
#include <atomic>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <cmath>

class CmdProcessor
{
public:
  struct Context
  {
    std::atomic<bool> *stream_active;
    struct sockaddr_in *video_client_addr;
    struct sockaddr_in *source_addr;
    V4L2H264Capture *capture;
  };

  struct Result
  {
    const char *response;
  };

  CmdProcessor()
  {
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&temp_config, &temp_sensor_) == ESP_OK)
    {
      temperature_sensor_enable(temp_sensor_);
    }
  }

  ~CmdProcessor()
  {
    cleanup();
  }

  Result process(const char *cmd, const Context &ctx)
  {
    if (strcmp(cmd, "info") == 0)
      return handleInfo(ctx);
    if (strcmp(cmd, "start") == 0)
      handleStart(ctx);
    else if (strcmp(cmd, "stop") == 0)
      handleStop(ctx);
    else if (strcmp(cmd, "reboot") == 0)
      handleReboot(ctx);
    else if (strcmp(cmd, "wifi_ap") == 0)
      handleWifiAP(ctx);
    else if (strncmp(cmd, "wifi_sta", 8) == 0)
      handleWifiSTA(cmd, ctx);
    else if (strncmp(cmd, "camera", 6) == 0)
      handleCamera(cmd, ctx);
    if (strcmp(cmd, "clear_error") == 0)
      handleClearError(ctx);
    else
      last_error_ = "unknown command";

    return {nullptr};
  }

private:
  static constexpr const char *TAG = "CMD_PROC";
  temperature_sensor_handle_t temp_sensor_ = nullptr;
  char info_buffer_[512] = {};
  std::string last_error_;

  void handleStart(const Context &ctx)
  {
    vTaskDelay(pdMS_TO_TICKS(100));
    *ctx.video_client_addr = *ctx.source_addr;
    ctx.stream_active->store(true);
  }

  void handleStop(const Context &ctx)
  {
    ctx.stream_active->store(false);
  }

  void handleReboot(const Context &ctx)
  {
    esp_restart();
  }

  void handleWifiAP(const Context &ctx)
  {
    wifi::set_mode(wifi::Mode::AP);
  }

  void handleWifiSTA(const char *cmd, const Context &ctx)
  {
    const char *delim1 = strstr(cmd, ":::");
    if (!delim1)
    {
      last_error_ = "wifi_sta requires SSID and password: wifi_sta:::SSID:::PASSWORD";
      return;
    }

    const char *ssid_start = delim1 + 3;
    const char *delim2 = strstr(ssid_start, ":::");

    std::string ssid, password;

    if (delim2)
    {
      ssid = std::string(ssid_start, delim2 - ssid_start);
      password = std::string(delim2 + 3);
    }
    else
    {
      ssid = std::string(ssid_start);
    }

    if (ssid.empty())
    {
      last_error_ = "SSID cannot be empty";
      return;
    }
    wifi::set_mode(wifi::Mode::STA, {.ssid = ssid, .password = password});
  }

  void handleCamera(const char *cmd, const Context &ctx)
  {
    if (!ctx.capture)
    {
      last_error_ = "camera not available";
      return;
    }

    int quality = -1, exposure = -1;
    parseCameraParams(cmd, quality, exposure);

    if (quality < 0 && exposure < 0)
    {
      last_error_ = "no valid parameters. Use: camera:::qual:VALUE:::exp:VALUE";
      return;
    }

    // Apply configuration directly to capture object
    V4L2H264Capture::Config config = ctx.capture->getConfig();

    if (quality >= 0)
      config.quality = quality;
    if (exposure >= 0)
      config.exposure = exposure;

    ctx.capture->updateConfig(config);
  }

  void parseCameraParams(const char *cmd, int &quality, int &exposure)
  {
    const char *pos = cmd;

    while (pos && *pos)
    {
      const char *next = strstr(pos, ":::");
      if (!next)
        break;

      pos = next + 3;

      if (strncmp(pos, "qual:", 5) == 0)
      {
        quality = atoi(pos + 5);
      }
      else if (strncmp(pos, "exp:", 4) == 0)
      {
        exposure = atoi(pos + 4);
      }
    }
  }

  Result handleInfo(const Context &ctx)
  {
    int64_t now_us = esp_timer_get_time();

    float temp = NAN;
    if (temp_sensor_)
    {
      temperature_sensor_get_celsius(temp_sensor_, &temp);
    }

    int8_t signal = wifi::get_signal_strength();
    size_t free_mem = esp_get_free_heap_size();
    size_t free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    const char *streaming_status = ctx.stream_active->load() ? "streaming" : "ready";
    const char *last_error = last_error_.empty() ? "" : last_error_.c_str();

    snprintf(info_buffer_, sizeof(info_buffer_),
             "{\"time\":%lld,\"temp\":%.2f,\"signal\":%d,\"free_heap\":%zu,\"free_block\":%zu,\"status\":\"%s\",\"last_error\":\"%s\"}",
             now_us, temp, signal, free_mem, free_block, streaming_status, last_error);

    return {info_buffer_};
  }

  void handleClearError(const Context &ctx)
  {
    last_error_.clear();
  }

  void cleanup()
  {
    if (temp_sensor_)
    {
      temperature_sensor_disable(temp_sensor_);
      temperature_sensor_uninstall(temp_sensor_);
      temp_sensor_ = nullptr;
    }
  }
};
