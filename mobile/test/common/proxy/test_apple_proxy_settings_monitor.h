#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include "library/common/network/apple_system_proxy_settings_monitor.h"
#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

class TestAppleSystemProxySettingsMonitor : public Network::AppleSystemProxySettingsMonitor {
public:
  TestAppleSystemProxySettingsMonitor(
      const std::string& host, const int port, bool use_pac_resolver,
      Network::SystemProxySettingsReadCallback proxy_settings_read_callback);
  virtual ~TestAppleSystemProxySettingsMonitor() = default;

protected:
  CFDictionaryRef getSystemProxySettings() const override;
  // Gets mock system proxy settings without a PAC file.
  CFDictionaryRef getSystemProxySettingsWithoutPac() const;
  // Gets mock system proxy settings with a PAC file.
  CFDictionaryRef getSystemProxySettingsWithPac() const;

  const std::string host_;
  const int port_;
  const bool use_pac_resolver_;
  // Used to pass into the Apple APIs to represent the number 1. We pass in pointers to the Apple
  // APIs, so the value needs to outline the passed-in pointer.
  const int one_{1};
};

} // namespace Network
} // namespace Envoy
