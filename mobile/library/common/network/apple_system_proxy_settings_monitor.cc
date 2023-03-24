#include "library/common/network/apple_system_proxy_settings_monitor.h"

#include <CFNetwork/CFNetwork.h>
#include <dispatch/dispatch.h>

namespace Envoy {
namespace Network {

// The interval at which system proxy settings should be polled at.
CFTimeInterval kProxySettingsRefreshRateSeconds = 7;

void AppleSystemProxySettingsMonitor::start() {
  if (started_) {
    return;
  }

  started_ = true;

  dispatch_queue_attr_t attributes = dispatch_queue_attr_make_with_qos_class(
      DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, DISPATCH_QUEUE_PRIORITY_DEFAULT);
  queue_ = dispatch_queue_create("io.envoyproxy.envoymobile.AppleSystemProxySettingsMonitor",
                                 attributes);

  __block absl::optional<SystemProxySettings> proxy_settings;
  dispatch_sync(queue_, ^{
    proxy_settings = readSystemProxySettings();
    proxySettingsDidUpdate_(proxy_settings);
  });

  source_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue_);
  dispatch_source_set_event_handler(source_, ^{
    const auto new_proxy_settings = readSystemProxySettings();
    if (new_proxy_settings != proxy_settings) {
      proxy_settings = new_proxy_settings;
      proxySettingsDidUpdate_(readSystemProxySettings());
    }
  });
  dispatch_resume(source_);
}

AppleSystemProxySettingsMonitor::~AppleSystemProxySettingsMonitor() {
  if (source_ != nullptr) {
    dispatch_suspend(source_);
  }
  if (queue_ != nullptr) {
    dispatch_release(queue_);
  }
}

absl::optional<SystemProxySettings>
AppleSystemProxySettingsMonitor::readSystemProxySettings() const {
  CFDictionaryRef proxy_settings = CFNetworkCopySystemProxySettings();

  // iOS system settings allow users to enter an arbitrary big integer number i.e. 88888888. That
  // being said, testing using iOS 16 shows that Apple's APIs return `is_http_proxy_enabled` equal
  // to false unless entered port number is within [0, 65535] range.
  const auto is_http_proxy_enabled = readIntValue(proxy_settings, kCFNetworkProxiesHTTPEnable) > 0;
  const auto is_auto_config_proxy_enabled =
      readIntValue(proxy_settings, kCFNetworkProxiesProxyAutoConfigEnable) > 0;

  absl::optional<SystemProxySettings> settings;
  if (is_http_proxy_enabled) {
    const auto hostname = readStringValue(proxy_settings, kCFNetworkProxiesHTTPProxy);
    const auto port = readIntValue(proxy_settings, kCFNetworkProxiesHTTPPort);
    settings = absl::make_optional<SystemProxySettings>({hostname, port});
  } else if (is_auto_config_proxy_enabled) {
    const auto pac_file_url =
        readStringValue(proxy_settings, kCFNetworkProxiesProxyAutoConfigURLString);
    settings = absl::make_optional(pac_file_url);
  }

  CFRelease(proxy_settings);
  return settings;
}

int AppleSystemProxySettingsMonitor::readIntValue(CFDictionaryRef dictionary,
                                                  CFStringRef key) const {
  CFNumberRef number = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
  if (number == nullptr) {
    return 0;
  }

  int value;
  CFNumberGetValue(number, kCFNumberSInt64Type, &value);
  return value;
}

std::string AppleSystemProxySettingsMonitor::readStringValue(CFDictionaryRef dictionary,
                                                             CFStringRef key) const {
  CFStringRef string = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, key));
  if (string == nullptr) {
    return "";
  }

  // A pointer to a C string or NULL if the internal storage of string
  // does not allow this to be returned efficiently.
  auto efficient_c_str = CFStringGetCStringPtr(string, kCFStringEncodingUTF8);
  if (efficient_c_str) {
    return std::string(efficient_c_str);
  }

  CFIndex length = CFStringGetLength(string);
  CFIndex size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  char* c_str = static_cast<char*>(malloc(size));
  // Use less efficient method of getting c string if the most performant one failed
  if (CFStringGetCString(string, c_str, size, kCFStringEncodingUTF8)) {
    const auto ret = std::string(c_str);
    free(c_str);
    return ret;
  }

  return "";
}

} // namespace Network
} // namespace Envoy
