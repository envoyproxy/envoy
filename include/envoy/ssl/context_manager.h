#pragma once

#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/stats/stats.h"

namespace Ssl {

/**
 * Manages all of the SSL contexts in the process
 */
class ContextManager {
public:
  virtual ~ContextManager() {}

  /**
   * Builds an ClientContext from an ContextConfig
   */
  virtual ClientContextPtr createSslClientContext(Stats::Scope& scope, ContextConfig& config) PURE;

  /**
   * Builds an ServerContext from an ContextConfig
   */
  virtual ServerContextPtr createSslServerContext(Stats::Scope& scope, ContextConfig& config) PURE;

  /**
   * @return the number of days until the next certificate being managed will expire
   */
  virtual size_t daysUntilFirstCertExpires() PURE;

  /**
   * @return a set of all contexts being managed
   */
  virtual std::vector<std::reference_wrapper<Context>> getContexts() PURE;
};

} // Ssl
