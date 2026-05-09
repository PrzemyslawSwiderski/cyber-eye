#pragma once

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <atomic>
#include <cstring>
#include <cstdint>

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

  static Result process(const char *cmd, const Context &ctx)
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

    return {"{\"error\":\"unknown command\"}"};
  }

private:
  static constexpr const char *TAG = "CMD_PROC";
};
