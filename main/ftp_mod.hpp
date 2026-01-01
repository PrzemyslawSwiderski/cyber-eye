#pragma once
#include <string>

namespace ftp
{
  void start_server_task(const std::string &ip_address);
  void stop_server_task();
}
