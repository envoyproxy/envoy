#include "library/common/network/system_proxy_settings.h"

namespace Envoy {
namespace Network {

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
