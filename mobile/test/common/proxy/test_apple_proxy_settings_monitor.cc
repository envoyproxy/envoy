#include "test/common/proxy/test_apple_proxy_settings_monitor.h"

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

namespace Envoy {
namespace test {

TestAppleSystemProxySettingsMonitor::TestAppleSystemProxySettingsMonitor(
    const std::string& host, const int port,
    Network::SystemProxySettingsReadCallback proxy_settings_read_callback)
    : AppleSystemProxySettingsMonitor(std::move(proxy_settings_read_callback)), host_(host),
      port_(port) {}

CFDictionaryRef TestAppleSystemProxySettingsMonitor::getSystemProxySettings() const {
  const void* keys[] = {kCFNetworkProxiesHTTPEnable, kCFNetworkProxiesHTTPProxy,
                        kCFNetworkProxiesHTTPPort};

  const int one = 1;
  const void* values[] = {
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &one),
      CFStringCreateWithCString(kCFAllocatorDefault, host_.c_str(), kCFStringEncodingUTF8),
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &port_)};

  int num_pairs = sizeof(keys) / sizeof(CFStringRef);

  CFDictionaryRef settings_dict = CFDictionaryCreate(
      kCFAllocatorDefault, static_cast<const void**>(keys), static_cast<const void**>(values),
      num_pairs, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  for (int i = 0; i < num_pairs; ++i) {
    CFRelease(values[i]);
  }
  return settings_dict;
}

} // namespace test
} // namespace Envoy
