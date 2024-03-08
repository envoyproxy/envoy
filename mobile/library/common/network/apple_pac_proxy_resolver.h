#pragma once

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <functional>
#include <vector>

#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

// The callback function for when the PAC file URL has been resolved and the configured proxies
// are available, if any.
// `ptr` contains the unowned pointer to the ProxySettingsResolvedCallback.
// `cf_proxies` is an array of the resolved proxies.
// `cf_error` is the error, if any, when trying to resolve the PAC file URL.
void proxyAutoConfigurationResultCallback(void* ptr, CFArrayRef cf_proxies, CFErrorRef cf_error);

/**
 * Resolves auto configuration (PAC) proxies.
 */
class ApplePacProxyResolver {
public:
  virtual ~ApplePacProxyResolver() = default;
  /**
   * Resolves proxy for a given URL using proxy auto configuration file that's hosted at a given
   * URL.
   * @param target_url A request URL to resolve the proxy for.
   * @param proxy_autoconfiguration_file_url A URL at which a proxy configuration file is hosted.
   * @param proxy_resolution_completed A function that's called with result proxies as its
   * arguments when proxy resolution completes.
   */
  void resolveProxies(const std::string& target_url,
                      const std::string& proxy_autoconfiguration_file_url,
                      ProxySettingsResolvedCallback proxy_resolution_completed);

protected:
  // Creates a CFRunLoopSourceRef for resolving the PAC file URL that the main CFRunLoop will run.
  // Implemented as a separate function and made virtual so we can overload in tests.
  virtual CFRunLoopSourceRef
  createPacUrlResolverSource(CFURLRef cf_proxy_autoconfiguration_file_url, CFURLRef cf_target_url,
                             CFStreamClientContext* context);
};

} // namespace Network
} // namespace Envoy
