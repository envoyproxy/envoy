#include "library/common/network/apple_pac_proxy_resolver.h"

#include <CFNetwork/CFNetwork.h>
#include <dispatch/dispatch.h>

#include "library/common/apple/utility.h"

namespace Envoy {
namespace Network {

void ApplePacProxyResolver::proxyAutoConfigurationResultCallback(void*, CFArrayRef cf_proxies, CFErrorRef cf_error) {
  // Note: we don't need the void* context because it typically contains the callback pointer,
  // whose ownership we'd be responsible for. However, we have stored the callback as a member
  // of ApplePacProxyResolver, so we don't need to worry about its lifetime (since it's tied to the
  // lifetime of this instance).  For that reason, we don't need the first argument to this
  // function.
  if (cf_error != nullptr || cf_proxies == nullptr) {
    // Treat the error case as if no proxy was configured. Seems to be consistent with what iOS
    // system (URLSession) is doing.
    proxy_resolution_completed_callback_({});
    return;
  }

  std::vector<ProxySettings> proxies;
  for (int i = 0; i < CFArrayGetCount(cf_proxies); i++) {
    CFDictionaryRef cf_dictionary =
        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(cf_proxies, i));
    CFStringRef cf_proxy_type =
        static_cast<CFStringRef>(CFDictionaryGetValue(cf_dictionary, kCFProxyTypeKey));
    bool is_http_proxy = CFStringCompare(cf_proxy_type, kCFProxyTypeHTTP, 0) == kCFCompareEqualTo;
    bool is_https_proxy = CFStringCompare(cf_proxy_type, kCFProxyTypeHTTPS, 0) == kCFCompareEqualTo;
    bool is_direct_proxy = CFStringCompare(cf_proxy_type, kCFProxyTypeNone, 0) == kCFCompareEqualTo;

    if (is_http_proxy || is_https_proxy) {
      CFStringRef cf_host =
          static_cast<CFStringRef>(CFDictionaryGetValue(cf_dictionary, kCFProxyHostNameKey));
      CFNumberRef cf_port =
          static_cast<CFNumberRef>(CFDictionaryGetValue(cf_dictionary, kCFProxyPortNumberKey));
      std::string hostname = Apple::toString(cf_host);
      int port = Apple::toInt(cf_port);
      proxies.push_back(ProxySettings(std::move(hostname), port));

    } else if (is_direct_proxy) {
      proxies.push_back(ProxySettings::direct());
    }
  }

  proxy_resolution_completed_callback_(proxies);
}

void ApplePacProxyResolver::resolveProxies(
    absl::string_view target_url_string, absl::string_view proxy_autoconfiguration_file_url_string,
    std::function<void(std::vector<ProxySettings>&)> proxy_resolution_did_complete) {
  proxy_resolution_completed_callback_ = proxy_resolution_did_complete;
  CFURLRef cf_target_url = createCFURL(target_url_string);
  CFURLRef cf_proxy_autoconfiguration_file_url =
      createCFURL(proxy_autoconfiguration_file_url_string);

  CFStreamClientContext context = {0, &proxy_resolution_completed_callback_, nullptr, nullptr, nullptr};
  // Even though neither the name of the method nor Apple's documentation mentions that, manual
  // testing shows that `CFNetworkExecuteProxyAutoConfigurationURL` method does caching of fetched
  // PAC file and does not fetch it on every proxy resolution request.
  CFRunLoopSourceRef run_loop_source =
      CFNetworkExecuteProxyAutoConfigurationURL(cf_proxy_autoconfiguration_file_url, cf_target_url,
                                                ApplePACProxyResolver::proxyAutoConfigurationResultCallback, &context);

  CFRunLoopAddSource(CFRunLoopGetMain(), run_loop_source, kCFRunLoopDefaultMode);

  CFRelease(cf_target_url);
  CFRelease(cf_proxy_autoconfiguration_file_url);
}

CFURLRef ApplePacProxyResolver::createCFURL(absl::string_view url_string) {
  auto cf_url_string =
      CFStringCreateWithCString(kCFAllocatorDefault, url_string.begin(), kCFStringEncodingUTF8);
  auto cf_url = CFURLCreateWithString(kCFAllocatorDefault, cf_url_string, NULL);
  CFRelease(cf_url_string);
  return cf_url;
}

} // namespace Network
} // namespace Envoy
