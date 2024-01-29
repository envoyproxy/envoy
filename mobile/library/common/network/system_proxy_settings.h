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
  SystemProxySettings(std::string&& hostname, int port);

  /**
   * Creates proxy settings using provided URL to a Proxy Auto-Configuration (PAC) file.
   * @param pac_file_url A URL to a Proxy Auto-Configuration (PAC) file.
   */
  SystemProxySettings(std::string&& pac_file_url);

  /**
   * @return a reference to the hostname (which will be an empty string if not configured).
   */
  const std::string& hostname() const;

  /*
   * @return the port number, or -1 if a PAC file is configured.
   */
  int port() const;

  /**
   * @return a reference to the PAC file URL (which will be an empty string if not configured).
   */
  const std::string& pacFileUrl() const;

  /**
   * @return returns true if this object represents a PAC file URL configured proxy,
   *         returns false if this object represents a host/port configured proxy.
   */
  bool isPacEnabled() const;

  /**
   * Equals operator overload.
   * @param rhs another SystemProxySettings object.
   * @return returns true if the two SystemProxySettings objects are equivalent (represents the
   *         same configuration); false otherwise.
   */
  bool operator==(SystemProxySettings const& rhs) const;

  /**
   * Not equals operator overload.
   * @param rhs another SystemProxySettings object.
   * @return returns true if the two SystemProxySettings objects are not equivalent (represents
   *         different proxy configuration); false otherwise.
   */
  bool operator!=(SystemProxySettings const& rhs) const;

private:
  std::string hostname_;
  int port_;
  std::string pac_file_url_;
};

} // namespace Network
} // namespace Envoy
