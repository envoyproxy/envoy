#include "library/common/network/apple_system_proxy_settings_monitor.h"

#include <CFNetwork/CFNetwork.h>
#include <CFNetwork/CFProxySupport.h>
#include <dispatch/dispatch.h>

#include "library/common/apple/utility.h"

namespace Envoy {
namespace Network {

// The interval at which system proxy settings should be polled at.
CFTimeInterval kProxySettingsRefreshRateSeconds = 10;

AppleSystemProxySettingsMonitor::AppleSystemProxySettingsMonitor(
    SystemProxySettingsReadCallback proxy_settings_read_callback)
    : proxy_settings_read_callback_(proxy_settings_read_callback) {}

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
    proxy_settings_read_callback_(std::move(proxy_settings));
  });

  source_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue_);
  dispatch_source_set_timer(source_, dispatch_time(DISPATCH_TIME_NOW, 0),
                            kProxySettingsRefreshRateSeconds * NSEC_PER_SEC, 0);
  dispatch_source_set_event_handler(source_, ^{
    const auto new_proxy_settings = readSystemProxySettings();
    if (new_proxy_settings != proxy_settings) {
      proxy_settings = new_proxy_settings;
      proxy_settings_read_callback_(std::move(new_proxy_settings));
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

CFDictionaryRef AppleSystemProxySettingsMonitor::getSystemProxySettings() const {
  return CFNetworkCopySystemProxySettings();
}

absl::optional<SystemProxySettings>
AppleSystemProxySettingsMonitor::readSystemProxySettings() const {
  CFDictionaryRef proxy_settings = getSystemProxySettings();
  if (proxy_settings == nullptr) {
    return absl::nullopt;
  }

  // iOS system settings allow users to enter an arbitrary big integer number i.e. 88888888 as a
  // port number. That being said, testing using iOS 16 shows that Apple's APIs return
  // `is_http_proxy_enabled` equal to false unless entered port number is within [0, 65535] range.
  CFNumberRef cf_is_http_proxy_enabled =
      static_cast<CFNumberRef>(CFDictionaryGetValue(proxy_settings, kCFNetworkProxiesHTTPEnable));
  CFNumberRef cf_is_auto_config_proxy_enabled = static_cast<CFNumberRef>(
      CFDictionaryGetValue(proxy_settings, kCFNetworkProxiesProxyAutoConfigEnable));
  const bool is_http_proxy_enabled = Apple::toInt(cf_is_http_proxy_enabled) > 0;
  const bool is_auto_config_proxy_enabled = Apple::toInt(cf_is_auto_config_proxy_enabled) > 0;

  absl::optional<SystemProxySettings> settings;
  if (is_http_proxy_enabled) {
    CFStringRef cf_hostname =
        static_cast<CFStringRef>(CFDictionaryGetValue(proxy_settings, kCFNetworkProxiesHTTPProxy));
    std::string hostname = Apple::toString(cf_hostname);
    CFNumberRef cf_port =
        static_cast<CFNumberRef>(CFDictionaryGetValue(proxy_settings, kCFNetworkProxiesHTTPPort));
    int port = Apple::toInt(cf_port);
    if (!hostname.empty() && port > 0) {
      settings = absl::make_optional<SystemProxySettings>(std::move(hostname), port);
    }
  } else if (is_auto_config_proxy_enabled) {
    CFStringRef cf_pac_file_url_string = static_cast<CFStringRef>(
        CFDictionaryGetValue(proxy_settings, kCFNetworkProxiesProxyAutoConfigURLString));
    std::string pac_file_url_str = Apple::toString(cf_pac_file_url_string);
    if (!pac_file_url_str.empty()) {
      settings = absl::make_optional<SystemProxySettings>(std::move(pac_file_url_str));
    }
  }

  CFRelease(proxy_settings);
  return settings;
}

} // namespace Network
} // namespace Envoy
