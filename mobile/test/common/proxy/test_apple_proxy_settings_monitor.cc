#include "test/common/proxy/test_apple_proxy_settings_monitor.h"

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Network {

TestAppleSystemProxySettingsMonitor::TestAppleSystemProxySettingsMonitor(
    const std::string& host, const int port, const bool use_pac_resolver,
    Network::SystemProxySettingsReadCallback proxy_settings_read_callback)
    : AppleSystemProxySettingsMonitor(std::move(proxy_settings_read_callback)), host_(host),
      port_(port), use_pac_resolver_(use_pac_resolver) {}

CFDictionaryRef TestAppleSystemProxySettingsMonitor::getSystemProxySettings() const {
  if (use_pac_resolver_) {
    return getSystemProxySettingsWithPac();
  }
  return getSystemProxySettingsWithoutPac();
}

CFDictionaryRef TestAppleSystemProxySettingsMonitor::getSystemProxySettingsWithoutPac() const {
  const void* keys[] = {kCFNetworkProxiesHTTPEnable, kCFNetworkProxiesHTTPProxy,
                        kCFNetworkProxiesHTTPPort};

  const void* values[] = {
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &one_),
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

CFDictionaryRef TestAppleSystemProxySettingsMonitor::getSystemProxySettingsWithPac() const {
  const void* keys[] = {kCFNetworkProxiesProxyAutoConfigEnable,
                        kCFNetworkProxiesProxyAutoConfigURLString};

  // Interpret the host + port as the location from which to obtain the PAC file.
  const std::string pac_file_url = absl::StrCat("http://", host_, ":", port_, "/proxy.pac");
  const void* values[] = {
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &one_),
      CFStringCreateWithCString(kCFAllocatorDefault, pac_file_url.c_str(), kCFStringEncodingUTF8)};

  int num_pairs = sizeof(keys) / sizeof(CFStringRef);

  CFDictionaryRef settings_dict = CFDictionaryCreate(
      kCFAllocatorDefault, static_cast<const void**>(keys), static_cast<const void**>(values),
      num_pairs, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  for (int i = 0; i < num_pairs; ++i) {
    CFRelease(values[i]);
  }
  return settings_dict;
}

} // namespace Network
} // namespace Envoy
