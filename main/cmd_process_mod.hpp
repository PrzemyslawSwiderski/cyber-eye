#pragma once

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"
#include "wifi_mod.hpp"
#include <atomic>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cmath>

class CmdProcessor
{
public:
  struct Context
  {
    std::atomic<bool> *stream_active;
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
    {
      ctx.stream_active->store(true);
      return {"{\"status\":\"started\"}"};
    }

    if (strcmp(cmd, "stop") == 0)
    {
      ctx.stream_active->store(false);
      return {"{\"status\":\"stopped\"}"};
    }

    if (strcmp(cmd, "status") == 0)
    {
      return {ctx.stream_active->load()
                  ? "{\"status\":\"streaming\"}"
                  : "{\"status\":\"stopped\"}"};
    }

    if (strcmp(cmd, "stats") == 0)
    {
      // Get current timestamp in microseconds for precise lag measurement
      int64_t now_us = esp_timer_get_time();

      // Get temperature from the sensor
      float temp = NAN;
      if (temp_sensor_)
      {
        temperature_sensor_get_celsius(temp_sensor_, &temp);
      }

      // Get signal strength from wifi_mod
      int8_t signal = wifi::get_signal_strength();

      // Total free heap memory (scattered)
      size_t free_mem = esp_get_free_heap_size();
      // Largest contiguous block (CRITICAL for video buffers!)
      size_t free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

      // Format JSON response with microsecond timestamp
      snprintf(stats_buffer_, sizeof(stats_buffer_),
               "{\"time\":%lld,\"temp\":%.2f,\"signal\":%d,\"free_heap\":%zu,\"free_block\":%zu}",
               now_us, temp, signal, free_mem, free_block);

      return {stats_buffer_};
    }

    return {"{\"error\":\"unknown command\"}"};
  }

private:
  static constexpr const char *TAG = "CMD_PROC";
  temperature_sensor_handle_t temp_sensor_ = nullptr;
  char stats_buffer_[256] = {};

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
