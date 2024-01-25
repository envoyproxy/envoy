#pragma once

#include <string>

namespace Envoy {
namespace Network {

/**
 * System proxy settings. There are 2 ways to specify desired proxy settings. Either provide proxy's
 * hostname or provide a URL to a Proxy Auto-Configuration (PAC) file.
 */
class SystemProxySettings {
public:
  /**
   * Creates proxy settings using provided hostname and port.
   * @param hostname A domain name or an IP address.
   * @param port     A valid internet port from within [0-65535] range.
   */
  SystemProxySettings(const std::string& hostname, int port) : hostname_(hostname), port_(port){};

  /**
   * Creates proxy settings using provided URL to a Proxy Auto-Configuration (PAC) file.
   * @param pac_file_url A URL to a Proxy Auto-Configuration (PAC) file.
   */
  SystemProxySettings(const std::string& pac_file_url) : port_(-1), pac_file_url_(pac_file_url){};

  std::string hostname() const { return hostname_; }
  int port() const { return port_; }
  std::string pacFileUrl() const { return pac_file_url_; }

  bool isPacEnabled() const { return pac_file_url_ != ""; }

  bool operator==(SystemProxySettings const& rhs) const {
    return hostname() == rhs.hostname() && port() == rhs.port() && pacFileUrl() == rhs.pacFileUrl();
  }

  bool operator!=(SystemProxySettings const& rhs) const { return !(*this == rhs); }

private:
  std::string hostname_;
  int port_;
  std::string pac_file_url_;
};

} // namespace Network
} // namespace Envoy
