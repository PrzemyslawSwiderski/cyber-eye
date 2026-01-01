#include "ftp_mod.hpp"
#include "ftp_server.hpp"
#include "task.hpp"
#include <stdexcept>
#include <string>

namespace ftp
{
  static std::unique_ptr<espp::FtpServer> ftp_server;
  static espp::Logger logger({.tag = "FTP", .level = espp::Logger::Verbosity::DEBUG});

  void start_server_task(const std::string &ip_address)
  {
    if (ftp_server)
    {
      logger.warn("FTP Server already running");
      return;
    }

    if (ip_address.empty())
    {
      logger.error("IP address parameter is empty");
      return;
    }

    logger.info("Initializing FTP Server on {}:21...", ip_address);

    try
    {
      ftp_server = std::make_unique<espp::FtpServer>(
          ip_address,
          21,
          espp::FileSystem::get().get_root_path());
      ftp_server->start();
      logger.info("FTP Server started successfully on {}", ip_address);
    }
    catch (const std::exception &e)
    {
      logger.error("Failed to start FTP server: {}", e.what());
      ftp_server.reset();
    }
  }

  void stop_server_task()
  {
    if (ftp_server)
    {
      logger.info("Stopping FTP Server...");
      ftp_server.reset(); // Destructor handles cleanup
      logger.info("FTP Server stopped");
    }
    else
    {
      logger.warn("FTP Server not running");
    }
  }

}
