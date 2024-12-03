#pragma once

#include <functional>

#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/stats/scope.h"

namespace Envoy {

namespace Server {
namespace Configuration {
class CommonFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Ssl {

using ContextAdditionalInitFunc =
    std::function<absl::Status(Ssl::TlsContext& context, const Ssl::TlsCertificateConfig& cert)>;

/**
 * Manages all of the SSL contexts in the process
 */
class ContextManager {
public:
  virtual ~ContextManager() = default;

  /**
   * Builds a ClientContext from a ClientContextConfig.
   */
  virtual absl::StatusOr<ClientContextSharedPtr>
  createSslClientContext(Stats::Scope& scope, const ClientContextConfig& config) PURE;

  /**
   * Builds a ServerContext from a ServerContextConfig.
   */
  virtual absl::StatusOr<ServerContextSharedPtr>
  createSslServerContext(Stats::Scope& scope, const ServerContextConfig& config,
                         const std::vector<std::string>& server_names,
                         ContextAdditionalInitFunc additional_init) PURE;

  /**
   * @return the number of days until the next certificate being managed will expire, the value is
   * set when not expired.
   */
  virtual absl::optional<uint32_t> daysUntilFirstCertExpires() const PURE;

  /**
   * Iterates through the contexts currently attached to a listener.
   */
  virtual void iterateContexts(std::function<void(const Context&)> callback) PURE;

  /**
   * Access the private key operations manager, which is part of SSL
   * context manager.
   */
  virtual PrivateKeyMethodManager& privateKeyMethodManager() PURE;

  /**
   * @return the number of seconds until the next OCSP response being managed will
   * expire, or `absl::nullopt` if no OCSP responses exist.
   */
  virtual absl::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const PURE;

  /**
   * Remove an existing ssl context.
   */
  virtual void removeContext(const Envoy::Ssl::ContextSharedPtr& old_context) PURE;
};

using ContextManagerPtr = std::unique_ptr<ContextManager>;

} // namespace Ssl
} // namespace Envoy
