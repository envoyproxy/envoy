
class SystemProxySettings {
  SystemProxySettings(const std::string hostname, const int port): hostname_(hostname), port_(port) {}

private:
  const std::string hostname_;
  const int port_;
}
