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
    std::function<void()> *deferred_action;
    V4L2H264Capture *capture; // Direct reference to capture object
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
    if (strcmp(cmd, "start") == 0)
      return handleStart(ctx);
    if (strcmp(cmd, "stop") == 0)
      return handleStop(ctx);
    if (strcmp(cmd, "reboot") == 0)
      return handleReboot(ctx);
    if (strcmp(cmd, "status") == 0)
      return handleStatus(ctx);
    if (strcmp(cmd, "wifi_ap") == 0)
      return handleWifiAP(ctx);
    if (strncmp(cmd, "wifi_sta", 8) == 0)
      return handleWifiSTA(cmd, ctx);
    if (strncmp(cmd, "camera", 6) == 0)
      return handleCamera(cmd, ctx);
    if (strcmp(cmd, "stats") == 0)
      return handleStats();

    return {"{\"error\":\"unknown command\"}"};
  }

private:
  static constexpr const char *TAG = "CMD_PROC";
  temperature_sensor_handle_t temp_sensor_ = nullptr;
  char stats_buffer_[256] = {};

  Result handleStart(const Context &ctx)
  {
    *ctx.deferred_action = [ctx]()
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      *ctx.video_client_addr = *ctx.source_addr;
      ctx.stream_active->store(true);
    };
    return {"{\"status\":\"ok\"}"};
  }

  Result handleStop(const Context &ctx)
  {
    *ctx.deferred_action = [ctx]()
    {
      ctx.stream_active->store(false);
    };
    return {"{\"status\":\"ok\"}"};
  }

  Result handleReboot(const Context &ctx)
  {
    *ctx.deferred_action = []()
    {
      esp_restart();
    };
    return {"{\"status\":\"ok\"}"};
  }

  Result handleStatus(const Context &ctx)
  {
    return {ctx.stream_active->load()
                ? "{\"status\":\"streaming\"}"
                : "{\"status\":\"ready\"}"};
  }

  Result handleWifiAP(const Context &ctx)
  {
    *ctx.deferred_action = []()
    {
      wifi::set_mode(wifi::Mode::AP);
    };
    return {"{\"status\":\"ok\"}"};
  }

  Result handleWifiSTA(const char *cmd, const Context &ctx)
  {
    const char *delim1 = strstr(cmd, ":::");
    if (!delim1)
    {
      return {"{\"error\":\"wifi_sta requires SSID and password: wifi_sta:::SSID:::PASSWORD\"}"};
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
      return {"{\"error\":\"SSID cannot be empty\"}"};
    }

    *ctx.deferred_action = [ssid = std::move(ssid), password = std::move(password)]()
    {
      wifi::set_mode(wifi::Mode::STA, {.ssid = ssid, .password = password});
    };
    return {"{\"status\":\"ok\"}"};
  }

  Result handleCamera(const char *cmd, const Context &ctx)
  {
    if (!ctx.capture)
    {
      return {"{\"error\":\"camera not available\"}"};
    }

    int quality = -1, exposure = -1, bitrate = -1;
    parseCameraParams(cmd, quality, exposure, bitrate);

    if (quality < 0 && exposure < 0 && bitrate < 0)
    {
      return {"{\"error\":\"no valid parameters. Use: camera:::qual:VALUE:::exp:VALUE:::bit:VALUE\"}"};
    }

    // Apply configuration directly to capture object
    V4L2H264Capture::Config config = ctx.capture->getConfig();

    if (quality >= 0)
      config.quality = quality;
    if (bitrate >= 0)
      config.bitrate = bitrate;
    if (exposure >= 0)
      config.exposure = exposure;
    // Note: exposure is applied during encoder configuration in init/start

    ctx.capture->updateConfig(config);

    snprintf(stats_buffer_, sizeof(stats_buffer_),
             "{\"status\":\"ok\",\"qual\":%d,\"exp\":%d,\"bit\":%d}",
             quality >= 0 ? quality : config.quality,
             exposure >= 0 ? exposure : config.exposure,
             bitrate >= 0 ? bitrate : config.bitrate);
    return {stats_buffer_};
  }

  void parseCameraParams(const char *cmd, int &quality, int &exposure, int &bitrate)
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
      else if (strncmp(pos, "bit:", 4) == 0)
      {
        bitrate = atoi(pos + 4);
      }
    }
  }

  Result handleStats()
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

    snprintf(stats_buffer_, sizeof(stats_buffer_),
             "{\"time\":%lld,\"temp\":%.2f,\"signal\":%d,\"free_heap\":%zu,\"free_block\":%zu}",
             now_us, temp, signal, free_mem, free_block);

    return {stats_buffer_};
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
