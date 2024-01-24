#pragma once

#include <CFNetwork/CFNetwork.h>

#include <functional>

#include "absl/strings/string_view.h"
#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

/**
 * Resolves auto configuration (PAC) proxies.
 */
class ApplePacProxyResolver {
public:
  /**
   * Resolves proxy for a given URL using proxy auto configuration file that's hosted at a given
   * URL.
   * @param target_url_string A request URL to resolve the proxy for.
   * @param proxy_autoconfiguration_file_url_string A URL at which a proxy configuration file is
   * hosted.
   * @param proxy_resolution_did_complete A function that's called with result proxies as its
   * arguments when proxy resolution completes.
   */
  void
  resolveProxies(absl::string_view target_url_string,
                 absl::string_view proxy_autoconfiguration_file_url_string,
                 std::function<void(std::vector<ProxySettings>&)> proxy_resolution_did_complete);

private:
  // Called when the PAC URL resolution has executed and the result is available.
  void proxyAutoConfigurationResultCallback(void* ptr, CFArrayRef cf_proxies, CFErrorRef cf_error);
  // Creates a CFURLRef from a C++ string URL.
  CFURLRef createCFURL(absl::string_view url_string);

  // The callback to invoke when PAC URL resolution is completed.
  std::function<void(std::vector<ProxySettings>&)> proxy_resolution_completed_callback_;
};

} // namespace Network
} // namespace Envoy
