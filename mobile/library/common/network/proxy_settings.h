#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "source/common/network/utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {

class ProxySettings;
class SystemProxySettings;
using ProxySettingsConstSharedPtr = std::shared_ptr<const ProxySettings>;
using ProxySettingsResolvedCallback = std::function<void(const std::vector<ProxySettings>&)>;
using SystemProxySettingsReadCallback = std::function<void(absl::optional<SystemProxySettings>)>;

/**
 * Proxy settings coming from platform specific APIs, i.e. ConnectivityManager in
 * the case of Android platform.
 *
 * ProxySettings represents already-resolved proxy settings. For example, if the proxy
 * settings are obtained from a PAC URL, then this object represents the resolved proxy
 * settings after reading and parsing the PAC URL.
 *
 * TODO(abeyad): rename this class to ProxySetting (the plural in the name is confusing).
 */
class ProxySettings {
public:
  /**
   * Construct a new Proxy Settings object.
   *
   * @param host The proxy host defined as a hostname or an IP address. Some platforms
   *             (i.e., Android) allow users to specify proxy using either one of these.
   * @param port The proxy port.
   */
  ProxySettings(const std::string& host, const uint16_t port);

  /**
   * Parses given host and domain and creates proxy settings. Returns nullptr for an empty host
   * and a port equal to 0, as they are passed to the C++ native layer to represent the lack of
   * proxy settings configured on a device.
   *
   * @param host The proxy host defined as a hostname or an IP address. Some platforms
   *             (i.e., Android) allow users to specify proxy using either one of these.
   * @param port The proxy port.
   * @return The created proxy settings, nullptr if the passed host is an empty string and
   *         port is equal to 0.
   */
  static ProxySettingsConstSharedPtr parseHostAndPort(const std::string& host, const uint16_t port);

  /**
   * Creates a ProxySettings instance to represent a direct connection (i.e. no proxy at all).
   *
   * @return The ProxySettings object. Calling isDirect() on it returns true.
   */
  static ProxySettings direct();

  /**
   * Creates a shared pointer instance of the ProxySettings to use, from the list of resolved
   * system proxy settings.
   *
   * @param settings_list A list of ProxySettings provided by the system.
   *
   * @return A shared pointer to an instance of the ProxySettings that was chosen from the proxy
   *         settings list.
   */
  static ProxySettingsConstSharedPtr create(const std::vector<ProxySettings>& settings_list);

  /**
   * @return true if the instance represents a direct connection (i.e. no proxy), false otherwise.
   */
  bool isDirect() const;

  /**
   * Returns an address of a proxy. This method returns nullptr for proxy settings that are
   * initialized with anything other than an IP address.
   *
   * @return Address of a proxy or nullptr if proxy address is incorrect or host is
   *         defined using a hostname and not an IP address.
   */
  const Envoy::Network::Address::InstanceConstSharedPtr& address() const;

  /**
   * Returns a reference to the hostname of the proxy.
   *
   * @return Hostname of the proxy (if the string is empty, there is no hostname).
   */
  const std::string& hostname() const;

  /**
   * Returns the port of the proxy.
   *
   * @return Port of the proxy.
   */
  uint16_t port() const;

  /**
   * Returns a human readable representation of the proxy settings represented by the receiver.
   *
   * @return const A human readable representation of the receiver.
   */
  const std::string asString() const;

  /**
   * Equals operator overload.
   * @param rhs another ProxySettings object.
   * @return returns true if the two ProxySettings objects are equivalent (represents the
   *         same configuration); false otherwise.
   */
  bool operator==(ProxySettings const& rhs) const;

  /**
   * Not equals operator overload.
   * @param rhs another ProxySettings object.
   * @return returns true if the two ProxySettings objects are not equivalent (represents
   *         different proxy configuration); false otherwise.
   */
  bool operator!=(ProxySettings const& rhs) const;

private:
  Envoy::Network::Address::InstanceConstSharedPtr address_;
  std::string hostname_;
  uint16_t port_;
};

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
