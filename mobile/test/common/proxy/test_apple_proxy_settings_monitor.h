#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include "library/common/network/apple_system_proxy_settings_monitor.h"
#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace test {

class TestAppleSystemProxySettingsMonitor : public Network::AppleSystemProxySettingsMonitor {
public:
  TestAppleSystemProxySettingsMonitor(
      const std::string& host, const int port,
      Network::SystemProxySettingsReadCallback proxy_settings_read_callback);
  virtual ~TestAppleSystemProxySettingsMonitor() = default;

protected:
  CFDictionaryRef getSystemProxySettings() const override;

  const std::string host_;
  const int port_;
};

} // namespace test
} // namespace Envoy
