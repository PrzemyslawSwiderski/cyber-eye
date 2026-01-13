#pragma once

#include <string>
#include <functional>

namespace wifi
{
  void start_wifi_task();
  void stop_wifi_task();
  int8_t get_signal_strength();
  bool is_connected();
  std::string get_ip();
}
