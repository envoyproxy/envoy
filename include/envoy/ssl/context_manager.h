#pragma once

#include <functional>

#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Ssl {

/**
 * Manages all of the SSL contexts in the process
 */
class ContextManager {
public:
  virtual ~ContextManager() {}

  /**
   * Builds an ClientContext from a Client ContextConfig.
   */
  virtual ClientContextPtr createSslClientContext(Stats::Scope& scope,
                                                  ClientContextConfig& config) PURE;

  /**
   * Builds an ServerContext from a ServerContextConfig.
   */
  virtual ServerContextPtr createSslServerContext(Stats::Scope& scope,
                                                  ServerContextConfig& config) PURE;

  /**
   * @return the number of days until the next certificate being managed will expire.
   */
  virtual size_t daysUntilFirstCertExpires() PURE;

  /**
   * Iterate through all currently allocated contexts.
   */
  virtual void iterateContexts(std::function<void(Context&)> callback) PURE;
};

} // namespace Ssl
} // namespace Envoy
