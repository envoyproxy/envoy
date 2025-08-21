#pragma once

#include "envoy/event/dispatcher.h"

#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

enum class ProxyResolutionResult {
  NoProxyConfigured = 0,
  ResultCompleted = 1,
  ResultInProgress = 2,
};

// An interface for resolving the system's proxy settings.
class ProxyResolver {
public:
  virtual ~ProxyResolver() = default;

  /**
   * Resolves proxy for a given url. Depending on the type current system proxy settings the method
   * may return results in synchronous or asynchronous way.
   * @param proxies Result proxies for when the proxy resolution is performed synchronously.
   * @param proxy_resolution_completed A function that's called with result proxies as its
   * arguments for when the proxy resolution is performed asynchronously.
   * @return Whether there is a proxy or no and whether proxy resolution was performed synchronously
   * or whether it's still running.
   */
  virtual ProxyResolutionResult
  resolveProxy(const std::string& target_url_string, std::vector<ProxySettings>& proxies,
               ProxySettingsResolvedCallback proxy_resolution_completed) PURE;

  /**
   * Sets the Event Dispatcher that a proxy resolver instance can use to post callbacks to.
   * Typically, this would be the main Envoy thread's dispatcher (which is also the worker thread
   * in Envoy Mobile).
   * @param dispatcher The event dispatcher.
   */
  virtual void setDispatcher(Event::Dispatcher* dispatcher) PURE;
};

} // namespace Network
} // namespace Envoy
