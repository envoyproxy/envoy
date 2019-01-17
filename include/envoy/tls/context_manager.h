#pragma once

#include <functional>

#include "envoy/stats/scope.h"
#include "envoy/tls/context.h"
#include "envoy/tls/context_config.h"

namespace Envoy {
namespace Tls {

/**
 * Manages all of the SSL contexts in the process
 */
class ContextManager {
public:
  virtual ~ContextManager() {}

  /**
   * Builds a ClientContext from a ClientContextConfig.
   */
  virtual ClientContextSharedPtr createSslClientContext(Stats::Scope& scope,
                                                        const ClientContextConfig& config) PURE;

  /**
   * Builds a ServerContext from a ServerContextConfig.
   */
  virtual ServerContextSharedPtr
  createSslServerContext(Stats::Scope& scope, const ServerContextConfig& config,
                         const std::vector<std::string>& server_names) PURE;

  /**
   * @return the number of days until the next certificate being managed will expire.
   */
  virtual size_t daysUntilFirstCertExpires() const PURE;

  /**
   * Iterate through all currently allocated contexts.
   */
  virtual void iterateContexts(std::function<void(const Context&)> callback) PURE;
};

} // namespace Tls
} // namespace Envoy
