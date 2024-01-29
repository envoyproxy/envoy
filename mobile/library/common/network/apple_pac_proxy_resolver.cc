#include "library/common/network/apple_pac_proxy_resolver.h"

#include <CFNetwork/CFNetwork.h>
#include <dispatch/dispatch.h>

#include "library/common/apple/utility.h"

namespace Envoy {
namespace Network {

namespace {

struct PacResultCallbackWrapper {
  PacResultCallbackWrapper(
      const std::function<void(std::vector<ProxySettings>&)>& proxy_resolution_did_complete)
      : pac_resolution_callback(proxy_resolution_did_complete) {}

  std::function<void(std::vector<ProxySettings>&)> pac_resolution_callback;
};

// Called when the PAC URL resolution has executed and the result is available.
static void proxyAutoConfigurationResultCallback(void* ptr, CFArrayRef cf_proxies,
                                                 CFErrorRef cf_error) {
  // `ptr` contains the unowned pointer to the PacResultCallbackWrapper. We extract it from the
  // void* and wrap it in a unique_ptr so the memory gets reclaimed at the end of the function when
  // `callback_wrapper` goes out of scope.
  std::unique_ptr<PacResultCallbackWrapper> callback_wrapper(
      static_cast<PacResultCallbackWrapper*>(ptr));

  std::vector<ProxySettings> proxies;
  if (cf_error != nullptr || cf_proxies == nullptr) {
    // Treat the error case as if no proxy was configured. Seems to be consistent with what iOS
    // system (URLSession) is doing.
    callback_wrapper->pac_resolution_callback(proxies);
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
      std::string hostname = Apple::toString(cf_host);
      int port = Apple::toInt(cf_port);
      proxies.emplace_back(ProxySettings(std::move(hostname), port));
    } else if (is_direct_proxy) {
      proxies.push_back(ProxySettings::direct());
    }
  }

  callback_wrapper->pac_resolution_callback(proxies);
}

// Creates a CFURLRef from a C++ string URL.
CFURLRef createCFURL(absl::string_view url_string) {
  auto cf_url_string =
      CFStringCreateWithCString(kCFAllocatorDefault, url_string.begin(), kCFStringEncodingUTF8);
  auto cf_url = CFURLCreateWithString(kCFAllocatorDefault, cf_url_string, /*baseURL=*/nullptr);
  CFRelease(cf_url_string);
  return cf_url;
}

} // namespace

void ApplePacProxyResolver::resolveProxies(
    absl::string_view target_url_string, absl::string_view proxy_autoconfiguration_file_url_string,
    std::function<void(std::vector<ProxySettings>&)> proxy_resolution_did_complete) {
  CFURLRef cf_target_url = createCFURL(target_url_string);
  CFURLRef cf_proxy_autoconfiguration_file_url =
      createCFURL(proxy_autoconfiguration_file_url_string);

  std::unique_ptr<PacResultCallbackWrapper> callback_wrapper =
      std::make_unique<PacResultCallbackWrapper>(proxy_resolution_did_complete);
  // According to https://developer.apple.com/documentation/corefoundation/cfstreamclientcontext,
  // the version must be 0.
  CFStreamClientContext context = {/*version=*/0, /*info=*/callback_wrapper.release(),
                                   /*retain=*/nullptr, /*release=*/nullptr,
                                   /*copyDescription=*/nullptr};
  // Even though neither the name of the method nor Apple's documentation mentions that, manual
  // testing shows that `CFNetworkExecuteProxyAutoConfigurationURL` method does caching of fetched
  // PAC file and does not fetch it on every proxy resolution request.
  CFRunLoopSourceRef run_loop_source =
      CFNetworkExecuteProxyAutoConfigurationURL(cf_proxy_autoconfiguration_file_url, cf_target_url,
                                                proxyAutoConfigurationResultCallback, &context);

  CFRunLoopAddSource(CFRunLoopGetMain(), run_loop_source, kCFRunLoopDefaultMode);

  CFRelease(cf_target_url);
  CFRelease(cf_proxy_autoconfiguration_file_url);
}

} // namespace Network
} // namespace Envoy
