#include "music_mod.hpp"
#include "control_mod.hpp"
#include "task.hpp"
#include "logger.hpp"
#include "udp_socket.hpp"
#include <memory>
#include <chrono>

using namespace std::chrono_literals;
namespace control
{
  static espp::Logger logger({.tag = "CONTROL", .level = espp::Logger::Verbosity::INFO});
  static std::unique_ptr<espp::UdpSocket> server_socket;

  // Private method for handling received UDP messages
  static std::optional<std::vector<uint8_t>> handle_udp_message(
      std::vector<uint8_t> &data,
      const espp::Socket::Info &sender_info)
  {
    // Convert data to string
    std::string received_message(data.begin(), data.end());

    // Remove trailing whitespace/newlines
    received_message.erase(received_message.find_last_not_of(" \n\r\t") + 1);

    fmt::print("Server received: '{}'\n"
               "    from source: {}\n",
               received_message, sender_info);

    std::string response;

    // Handle commands
    if (received_message == "PLAY")
    {
      logger.info("Executing PLAY command");
      music::play();
      response = "♫ MUSIC PLAYED 🎼 🎻 🎷";
    }
    else
    {
      response = "PONG";
    }

    // Return response
    return std::vector<uint8_t>(response.begin(), response.end());
  }

  void start_control_task()
  {
    if (server_socket)
    {
      logger.warn("UDP Control Server already running");
      return;
    }

    size_t port = 5000;
    server_socket = std::make_unique<espp::UdpSocket>(
        espp::UdpSocket::Config{.log_level = espp::Logger::Verbosity::WARN});

    auto server_task_config = espp::Task::BaseConfig{
        .name = "UdpServer",
        .priority = 7};

    auto server_config = espp::UdpSocket::ReceiveConfig{
        .port = port,
        .buffer_size = 1024,
        .on_receive_callback = handle_udp_message};

    server_socket->start_receiving(server_task_config, server_config);
    logger.info("UDP Control Server started on port {}", port);
    // Send a command from your computer
    // echo "test command" | nc -u 192.168.33.27 5000
  }

  void stop_control_task()
  {
    if (server_socket)
    {
      logger.info("Stopping UDP Control Server...");
      server_socket.reset();
      logger.info("UDP Control Server stopped");
    }
  }
}
