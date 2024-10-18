#include "library/common/network/apple_pac_proxy_resolver.h"

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>

#include "source/common/common/assert.h"

#include "library/common/apple/utility.h"

namespace Envoy {
namespace Network {

namespace {

CFStringRef pacRunLoopMode() {
  static const CFStringRef runloop_mode = CFSTR("envoy.PacProxyResolver");
  return runloop_mode;
}

// Creates a CFURLRef from a C++ string URL.
CFURLRef createCFURL(const std::string& url_string) {
  return CFURLCreateWithBytes(kCFAllocatorDefault,
                              reinterpret_cast<const UInt8*>(url_string.c_str()),
                              url_string.length(), kCFStringEncodingUTF8, nullptr);
}

} // namespace

void proxyAutoConfigurationResultCallback(void* ptr, CFArrayRef cf_proxies, CFErrorRef cf_error) {
  // `ptr` contains the unowned pointer to the ProxySettingsResolvedCallback. We extract it from the
  // void* and wrap it in a unique_ptr so the memory gets reclaimed at the end of the function when
  // `completion_callback` goes out of scope.
  std::cerr << "=========>>>> AAB proxyAutoConfigurationResultCallback" << std::endl;
  std::unique_ptr<ProxySettingsResolvedCallback> completion_callback(
      static_cast<ProxySettingsResolvedCallback*>(ptr));
  if (cf_proxies != nullptr) std::cerr << "=========>>>> AAB proxyAutoConfigurationResultCallback proxies.size=" << CFArrayGetCount(cf_proxies) << std::endl;
  else std::cerr << "=========>>>> AAB proxyAutoConfigurationResultCallback cf_proxies IS NULL" << std::endl;
  if (cf_error != nullptr) std::cerr << "=========>>>> AAB proxyAutoConfigurationResultCallback ERROR: " << Apple::toString(CFErrorCopyDescription(cf_error)) << std::endl;

  std::vector<ProxySettings> proxies;
  if (cf_error != nullptr || cf_proxies == nullptr) {
    if (cf_error != nullptr) std::cerr << "=========>>>> AAB proxyAutoConfigurationResultCallback ERROR: " << Apple::toString(CFErrorCopyDescription(cf_error)) << std::endl;
    ENVOY_BUG(cf_error != nullptr, Apple::toString(CFErrorCopyDescription(cf_error)));
    // Treat the error case as if no proxy was configured. Seems to be consistent with what iOS
    // system (URLSession) is doing.
    (*completion_callback)(proxies);
    CFRunLoopStop(CFRunLoopGetCurrent());
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

  std::cerr << "========>>>> AAB proxyAutoConfigurationResultCallback, size=" << proxies.size() << std::endl;
  for (const auto& x : proxies) std::cerr << "========>>>> AAB proxyAutoConfigurationResultCallback, proxy setting=" << x.asString() << std::endl;
  (*completion_callback)(proxies);
  CFRunLoopStop(CFRunLoopGetCurrent());
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
    const std::string& proxy_autoconfiguration_file_url, const std::string& target_url,
    ProxySettingsResolvedCallback proxy_resolution_completed) {
  std::cerr << "=========>>>> AAB ApplePacProxyResolver::resolveProxies, target_url=" << target_url << ", proxy url: " << proxy_autoconfiguration_file_url << std::endl;
  CFURLRef cf_target_url = createCFURL(target_url);
  CFURLRef cf_proxy_autoconfiguration_file_url = createCFURL(proxy_autoconfiguration_file_url);
  if (cf_target_url == nullptr || cf_proxy_autoconfiguration_file_url == nullptr) {
    return;
  }

  std::unique_ptr<ProxySettingsResolvedCallback> completion_callback =
      std::make_unique<ProxySettingsResolvedCallback>(std::move(proxy_resolution_completed));

  // Work around <rdar://problem/5530166>. This dummy call to CFNetworkCopyProxiesForURL
  // initializes some state within CFNetwork that is required by
  // CFNetworkExecuteProxyAutoConfigurationURL.
  CFDictionaryRef empty_dictionary = CFDictionaryCreate(nullptr, nullptr, nullptr, 0, nullptr, nullptr);
  CFArrayRef empty_result = CFNetworkCopyProxiesForURL(cf_target_url, empty_dictionary);
  if (empty_result) {
    CFRelease(empty_result);
  }
  CFRelease(empty_dictionary);

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

  CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source, pacRunLoopMode());
  CFRunLoopRunInMode(pacRunLoopMode(), DBL_MAX, false);
  CFRunLoopRemoveSource(CFRunLoopGetCurrent(), run_loop_source, pacRunLoopMode());

  CFRelease(cf_target_url);
  CFRelease(cf_proxy_autoconfiguration_file_url);
  CFRelease(run_loop_source);
}

} // namespace Network
} // namespace Envoy
