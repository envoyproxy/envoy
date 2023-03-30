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
class ApplePACProxyResolver {
public:
  /**
   * Resolves proxy for a given url using proxy auto configuration file that's hosted at a given
   * url.
   * @param target_url_string A request url to resolve the proxy for.
   * @param proxy_autoconfiguration_file_url_string A url at which a proxy configuration file is
   * hosted.
   * @param proxy_resolution_did_complete A function that's called with result proxies as its
   * arguments when proxy resolution completes.
   */
  void
  resolveProxies(absl::string_view target_url_string,
                 absl::string_view proxy_autoconfiguration_file_url_string,
                 std::function<void(std::vector<ProxySettings>)> proxy_resolution_did_complete);

private:
  CFURLRef createCFURL(absl::string_view);
};

} // namespace Network
} // namespace Envoy
