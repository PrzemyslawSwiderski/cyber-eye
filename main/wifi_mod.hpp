#pragma once

#include <string>
#include <functional>

namespace wifi
{
  void start_wifi_task();
  void stop_wifi_task();
  void set_on_got_ip_callback(std::function<void(const std::string &)> callback);
  int8_t get_signal_strength();
}
