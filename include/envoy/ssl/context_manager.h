#pragma once

#include <functional>

#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Ssl {

/**
 * Manages all of the SSL contexts in the process
 */
class ContextManager {
public:
  virtual ~ContextManager() = default;

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

  /**
   * Access the private key operations manager, which is part of SSL
   * context manager.
   */
  virtual PrivateKeyMethodManager& privateKeyMethodManager() PURE;
};

using ContextManagerPtr = std::unique_ptr<ContextManager>;

class ContextManagerFactory : public Config::UntypedFactory {
public:
  virtual ~ContextManagerFactory() = default;
  virtual ContextManagerPtr createContextManager(TimeSource& time_source) PURE;

  // There could be only one factory thus the name is static.
  std::string name() const override { return "ssl_context_manager"; }
  std::string category() const override { return "envoy.ssl_context_manager"; }
};

} // namespace Ssl
} // namespace Envoy
