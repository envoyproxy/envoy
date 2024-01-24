#pragma once

#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

enum class ProxyResolutionResult {
  NO_PROXY_CONFIGURED = 0,
  RESULT_COMPLETED = 1,
  RESULT_IN_PROGRESS = 2,
};

// An interface for resolving the system's proxy settings.
class ProxyResolver {
public:
  virtual ~ProxyResolver() = default;

  /**
   * Resolves proxy for a given url. Depending on the type current system proxy settings the method
   * may return results in synchronous or asynchronous way.
   * @param proxies Result proxies for when the proxy resolution is performed synchronously.
   * @param proxy_resolution_did_complete A function that's called with result proxies as its
   * arguments for when the proxy resolution is performed asynchronously.
   * @return Whether there is a proxy or no and whether proxy resolution was performed synchronously
   * or whether it's still running.
   */
  virtual ProxyResolutionResult resolveProxy(
      const std::string& target_url_string, std::vector<ProxySettings>& proxies,
      std::function<void(std::vector<ProxySettings>& proxies)> proxy_resolution_did_complete) PURE;
};

} // namespace Network
} // namespace Envoy
