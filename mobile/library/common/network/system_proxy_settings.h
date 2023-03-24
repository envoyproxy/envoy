#include <string>

namespace Envoy {
namespace Network {

class SystemProxySettings {
public:
  SystemProxySettings(const std::string& hostname, int port) : hostname_(hostname), port_(port){};
  SystemProxySettings(const std::string& pac_file_url) : port_(-1), pac_file_url_(pac_file_url){};

  std::string hostname() const { return hostname_; }
  int port() const { return port_; }
  std::string pacFileURL() const { return pac_file_url_; }

  bool operator==(SystemProxySettings const& rhs) const {
    return hostname() == rhs.hostname() && port() == rhs.port() && pacFileURL() == rhs.pacFileURL();
  }

  bool operator!=(SystemProxySettings const& rhs) const { return !(*this == rhs); }

private:
  std::string hostname_;
  int port_;
  std::string pac_file_url_;
};

} // namespace Network
} // namespace Envoy
