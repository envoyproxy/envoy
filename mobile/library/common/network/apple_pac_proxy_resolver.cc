#include "library/common/network/apple_pac_proxy_resolver.h"

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

#include "source/common/common/assert.h"

#include "library/common/apple/utility.h"

namespace Envoy {
namespace Network {

namespace {

// Creates a CFURLRef from a C++ string URL.
CFURLRef createCFURL(const std::string& url_string) {
  auto cf_url_string =
      CFStringCreateWithCString(kCFAllocatorDefault, url_string.c_str(), kCFStringEncodingUTF8);
  auto cf_url = CFURLCreateWithString(kCFAllocatorDefault, cf_url_string, /*baseURL=*/nullptr);
  CFRelease(cf_url_string);
  return cf_url;
}

} // namespace

void proxyAutoConfigurationResultCallback(void* ptr, CFArrayRef cf_proxies, CFErrorRef cf_error) {
  // `ptr` contains the unowned pointer to the ProxySettingsResolvedCallback. We extract it from the
  // void* and wrap it in a unique_ptr so the memory gets reclaimed at the end of the function when
  // `completion_callback` goes out of scope.
  std::unique_ptr<ProxySettingsResolvedCallback> completion_callback(
      static_cast<ProxySettingsResolvedCallback*>(ptr));

  std::vector<ProxySettings> proxies;
  if (cf_error != nullptr || cf_proxies == nullptr) {
    ENVOY_BUG(cf_error != nullptr, Apple::toString(CFErrorCopyDescription(cf_error)));
    // Treat the error case as if no proxy was configured. Seems to be consistent with what iOS
    // system (URLSession) is doing.
    (*completion_callback)(proxies);
    return;
  }

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
      std::string host = Apple::toString(cf_host);
      int port = Apple::toInt(cf_port);
      if (!host.empty() && port > 0) {
        proxies.emplace_back(ProxySettings(std::move(host), port));
      }
    } else if (is_direct_proxy) {
      proxies.push_back(ProxySettings::direct());
    }
  }

  (*completion_callback)(proxies);
}

CFRunLoopSourceRef
ApplePacProxyResolver::createPacUrlResolverSource(CFURLRef cf_proxy_autoconfiguration_file_url,
                                                  CFURLRef cf_target_url,
                                                  CFStreamClientContext* context) {
  // Even though neither the name of the method nor Apple's documentation mentions that, manual
  // testing shows that `CFNetworkExecuteProxyAutoConfigurationURL` method does caching of fetched
  // PAC file and does not fetch it on every proxy resolution request.
  return CFNetworkExecuteProxyAutoConfigurationURL(cf_proxy_autoconfiguration_file_url,
                                                   cf_target_url,
                                                   proxyAutoConfigurationResultCallback, context);
}

void ApplePacProxyResolver::resolveProxies(
    const std::string& target_url, const std::string& proxy_autoconfiguration_file_url,
    ProxySettingsResolvedCallback proxy_resolution_completed) {
  CFURLRef cf_target_url = createCFURL(target_url);
  CFURLRef cf_proxy_autoconfiguration_file_url = createCFURL(proxy_autoconfiguration_file_url);

  std::unique_ptr<ProxySettingsResolvedCallback> completion_callback =
      std::make_unique<ProxySettingsResolvedCallback>(std::move(proxy_resolution_completed));
  // According to https://developer.apple.com/documentation/corefoundation/cfstreamclientcontext,
  // the version must be 0.
  auto context = std::make_unique<CFStreamClientContext>(
      CFStreamClientContext{/*version=*/0,
                            /*info=*/completion_callback.release(),
                            /*retain=*/nullptr,
                            /*release=*/nullptr,
                            /*copyDescription=*/nullptr});

  // Ownership of the context gets released to the CFRunLoopSourceRef. When
  // `proxyAutoConfigurationResultCallback` gets invoked, the pointer is passed in and is
  // responsible for releasing the memory.
  CFRunLoopSourceRef run_loop_source = createPacUrlResolverSource(
      cf_proxy_autoconfiguration_file_url, cf_target_url, context.release());

  CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source, kCFRunLoopDefaultMode);

  CFRelease(cf_target_url);
  CFRelease(cf_proxy_autoconfiguration_file_url);
  CFRelease(run_loop_source);
}

} // namespace Network
} // namespace Envoy
