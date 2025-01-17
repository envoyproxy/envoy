#pragma once

#include <cstdint>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/ssl/private_key/private_key_callbacks.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/ssl/ssl_socket_state.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/ssl_handshaker.h"
#include "source/common/tls/ssl_socket.h"
#include "source/common/tls/utility.h"

#include "absl/container/node_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class ServerSslSocketFactory : public Network::DownstreamTransportSocketFactory,
                               public Secret::SecretCallbacks,
                               Logger::Loggable<Logger::Id::config> {
public:
  static absl::StatusOr<std::unique_ptr<ServerSslSocketFactory>>
  create(Envoy::Ssl::ServerContextConfigPtr config, Envoy::Ssl::ContextManager& manager,
         Stats::Scope& stats_scope, const std::vector<std::string>& server_names);

  ~ServerSslSocketFactory() override;

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
  bool implementsSecureTransport() const override;

  // Secret::SecretCallbacks
  absl::Status onAddOrUpdateSecret() override;

protected:
  ServerSslSocketFactory(Envoy::Ssl::ServerContextConfigPtr config,
                         Envoy::Ssl::ContextManager& manager, Stats::Scope& stats_scope,
                         const std::vector<std::string>& server_names,
                         absl::Status& creation_status);

private:
  Ssl::ContextManager& manager_;
  Stats::Scope& stats_scope_;
  SslSocketFactoryStats stats_;
  Envoy::Ssl::ServerContextConfigPtr config_;
  const std::vector<std::string> server_names_;
  mutable absl::Mutex ssl_ctx_mu_;
  Envoy::Ssl::ServerContextSharedPtr ssl_ctx_ ABSL_GUARDED_BY(ssl_ctx_mu_);
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
