#pragma once

#include <functional>
#include <vector>

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
};

} // namespace Network
} // namespace Envoy
