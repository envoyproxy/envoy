#include "library/common/network/proxy_settings.h"

#include <memory>

namespace Envoy {
namespace Network {

ProxySettings::ProxySettings(const std::string& host, const uint16_t port)
    : address_(Envoy::Network::Utility::parseInternetAddressNoThrow(host, port)), hostname_(host),
      port_(port) {}

/*static*/
ProxySettingsConstSharedPtr ProxySettings::parseHostAndPort(const std::string& host,
                                                            const uint16_t port) {
  if (host == "" && port == 0) {
    return nullptr;
  }
  return std::make_shared<ProxySettings>(host, port);
}

/*static*/
ProxySettings ProxySettings::direct() { return ProxySettings("", 0); }

/*static*/
ProxySettingsConstSharedPtr ProxySettings::create(const std::vector<ProxySettings>& settings_list) {
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

bool ProxySettings::isDirect() const { return hostname_ == "" && port_ == 0; }

const Envoy::Network::Address::InstanceConstSharedPtr& ProxySettings::address() const {
  return address_;
}

const std::string& ProxySettings::hostname() const { return hostname_; }

uint16_t ProxySettings::port() const { return port_; }

const std::string ProxySettings::asString() const {
  if (address_ != nullptr) {
    return address_->asString();
  }
  if (!hostname_.empty()) {
    return absl::StrCat(hostname_, ":", port_);
  }
  return "no_proxy_configured";
}

bool ProxySettings::operator==(ProxySettings const& rhs) const {
  // Even if the hostnames are IP addresses, they'll be stored in hostname_
  return hostname() == rhs.hostname() && port() == rhs.port();
}

bool ProxySettings::operator!=(ProxySettings const& rhs) const { return !(*this == rhs); }

SystemProxySettings::SystemProxySettings(std::string&& hostname, int port)
    : hostname_(std::move(hostname)), port_(port) {}

SystemProxySettings::SystemProxySettings(std::string&& pac_file_url)
    : port_(-1), pac_file_url_(std::move(pac_file_url)) {}

const std::string& SystemProxySettings::hostname() const { return hostname_; }

int SystemProxySettings::port() const { return port_; }

const std::string& SystemProxySettings::pacFileUrl() const { return pac_file_url_; }

bool SystemProxySettings::isPacEnabled() const { return !pac_file_url_.empty(); }

bool SystemProxySettings::operator==(SystemProxySettings const& rhs) const {
  return hostname() == rhs.hostname() && port() == rhs.port() && pacFileUrl() == rhs.pacFileUrl();
}

bool SystemProxySettings::operator!=(SystemProxySettings const& rhs) const {
  return !(*this == rhs);
}

} // namespace Network
} // namespace Envoy
