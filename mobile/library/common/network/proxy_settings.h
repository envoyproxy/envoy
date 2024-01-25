#pragma once

#include "source/common/network/utility.h"

namespace Envoy {
namespace Network {

class ProxySettings;
using ProxySettingsConstSharedPtr = std::shared_ptr<const ProxySettings>;

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
  ProxySettings(const std::string& host, const uint16_t port)
      : address_(Envoy::Network::Utility::parseInternetAddressNoThrow(host, port)), hostname_(host),
        port_(port) {}

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
  static const ProxySettingsConstSharedPtr parseHostAndPort(const std::string& host,
                                                            const uint16_t port) {
    if (host == "" && port == 0) {
      return nullptr;
    }
    return std::make_shared<ProxySettings>(host, port);
  }

  /**
   * Creates a ProxySettings instance to represent a direct connection (i.e. no proxy at all).
   *
   * @return The ProxySettings object. Calling isDirect() on it returns true.
   */
  static ProxySettings direct() { return ProxySettings("", 0); }

  /**
   * @return true if the instance represents a direct connection (i.e. no proxy), false otherwise.
   */
  bool isDirect() const { return hostname_ == "" && port_ == 0; }

  /**
   * Creates a shared pointer instance of the ProxySettings to use, from the list of resolved
   * system proxy settings.
   *
   * @param settings_list A list of ProxySettings provided by the system.
   *
   * @return A shared pointer to an instance of the ProxySettings that was chosen from the proxy
   *         settings list.
   */
  static const ProxySettingsConstSharedPtr create(const std::vector<ProxySettings>& settings_list) {
    if (settings_list.empty()) {
      return nullptr;
    }

    for (const auto& proxy_settings : settings_list) {
      if (!proxy_settings.isDirect()) {
        // We'll use the first non-direct ProxySettings.
        return std::make_shared<ProxySettings>(proxy_settings.hostname(), proxy_settings.port());
      }
    }

    // No non-direct ProxySettings was found.
    return nullptr;
  }

  /**
   * Returns an address of a proxy. This method returns nullptr for proxy settings that are
   * initialized with anything other than an IP address.
   *
   * @return Address of a proxy or nullptr if proxy address is incorrect or host is
   *         defined using a hostname and not an IP address.
   */
  const Envoy::Network::Address::InstanceConstSharedPtr& address() const { return address_; }

  /**
   * Returns a reference to the hostname of the proxy.
   *
   * @return Hostname of the proxy (if the string is empty, there is no hostname).
   */
  const std::string& hostname() const { return hostname_; }

  /**
   * Returns the port of the proxy.
   *
   * @return Port of the proxy.
   */
  uint16_t port() const { return port_; }

  /**
   * Returns a human readable representation of the proxy settings represented by the receiver.
   *
   * @return const A human readable representation of the receiver.
   */
  const std::string asString() const {
    if (address_ != nullptr) {
      return address_->asString();
    }
    if (!hostname_.empty()) {
      return absl::StrCat(hostname_, ":", port_);
    }
    return "no_proxy_configured";
  }

  bool operator==(ProxySettings const& rhs) const {
    // Even if the hostnames are IP addresses, they'll be stored in hostname_
    return hostname() == rhs.hostname() && port() == rhs.port();
  }

  bool operator!=(ProxySettings const& rhs) const { return !(*this == rhs); }

private:
  Envoy::Network::Address::InstanceConstSharedPtr address_;
  std::string hostname_;
  uint16_t port_;
};

} // namespace Network
} // namespace Envoy
