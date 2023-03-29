#include "library/common/network/apple_pac_proxy_resolver.h"

#include <CFNetwork/CFNetwork.h>
#include <dispatch/dispatch.h>

#include "library/common/apple/utility.h"

namespace Envoy {
namespace Network {

class PACProxyResolutionCompletionCallback {
public:
  PACProxyResolutionCompletionCallback(std::function<void(std::vector<ProxySettings>)> callback)
      : callback_(callback){};
  std::function<void(std::vector<ProxySettings>)> callback_;
};

static void proxyAutoConfigurationResultCallback(void* ptr, CFArrayRef cf_proxies,
                                                 CFErrorRef cf_error) {
  auto completion_callback = static_cast<PACProxyResolutionCompletionCallback*>(ptr);
  auto completion = completion_callback->callback_;
  delete completion_callback;

  if (cf_error != nullptr || cf_proxies == nullptr) {
    // Treat the error case as if no proxy was configured. Seems to be consistent with what iOS
    // system (URLSession) is doing.
    completion({});
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
    bool is_no_proxy = CFStringCompare(cf_proxy_type, kCFProxyTypeNone, 0) == kCFCompareEqualTo;

    if (is_http_proxy || is_https_proxy) {
      CFStringRef cf_host =
          static_cast<CFStringRef>(CFDictionaryGetValue(cf_dictionary, kCFProxyHostNameKey));
      CFNumberRef cf_port =
          static_cast<CFNumberRef>(CFDictionaryGetValue(cf_dictionary, kCFProxyPortNumberKey));
      std::string hostname = Apple::toString(cf_host);
      int port = Apple::toInt(cf_port);
      proxies.push_back(ProxySettings(std::move(hostname), port));

    } else if (is_no_proxy) {
      // TODO(Augustyniak) add an empty proxy to the list of proxies instead of doing nothing
      continue;
    }
  }
  completion(proxies);
}

void ApplePACProxyResolver::resolveProxies(
    absl::string_view target_url_string, absl::string_view proxy_autoconfiguration_file_url_string,
    std::function<void(std::vector<ProxySettings>)> didResolveProxy) {
  CFURLRef cf_target_url = createCFURL(target_url_string);
  CFURLRef cf_proxy_autoconfiguration_file_url =
      createCFURL(proxy_autoconfiguration_file_url_string);

  auto callbackWrapper =
      static_cast<void*>(new class PACProxyResolutionCompletionCallback(didResolveProxy));
  CFStreamClientContext context = {0, callbackWrapper, nullptr, nullptr, nullptr};
  CFRunLoopSourceRef runLoopSource =
      CFNetworkExecuteProxyAutoConfigurationURL(cf_proxy_autoconfiguration_file_url, cf_target_url,
                                                proxyAutoConfigurationResultCallback, &context);

  CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopDefaultMode);

  CFRelease(cf_target_url);
  CFRelease(cf_proxy_autoconfiguration_file_url);
}

CFURLRef ApplePACProxyResolver::createCFURL(absl::string_view url_string) {
  auto cf_url_string =
      CFStringCreateWithCString(kCFAllocatorDefault, url_string.begin(), kCFStringEncodingUTF8);
  auto cf_url = CFURLCreateWithString(kCFAllocatorDefault, cf_url_string, NULL);
  CFRelease(cf_url_string);
  return cf_url;
}

} // namespace Network
} // namespace Envoy
