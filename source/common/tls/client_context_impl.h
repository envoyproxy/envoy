#pragma once

#include <openssl/safestack.h>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/matchers.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/stats.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

#ifdef ENVOY_ENABLE_QUIC
#include "quiche/quic/core/crypto/proof_source.h"
#endif

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class ClientContextImpl : public ContextImpl, public Envoy::Ssl::ClientContext {
public:
  static absl::StatusOr<std::unique_ptr<ClientContextImpl>>
  create(Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
         Server::Configuration::CommonFactoryContext& factory_context);

  absl::StatusOr<bssl::UniquePtr<SSL>>
  newSsl(const Network::TransportSocketOptionsConstSharedPtr& options,
         const Upstream::HostDescriptionConstSharedPtr& host) override;

private:
  ClientContextImpl(Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
                    Server::Configuration::CommonFactoryContext& factory_context,
                    absl::Status& creation_status);

  int newSessionKey(SSL_SESSION* session, const Upstream::HostDescriptionConstSharedPtr& host);
  static int sslSocketUpstreamHostIndex();
  uint64_t sessionCacheKey(const Upstream::HostDescriptionConstSharedPtr& host);

  const std::string server_name_indication_;
  const bool auto_host_sni_;
  const bool allow_renegotiation_;
  const bool enforce_rsa_key_usage_;
  const size_t max_session_keys_;
  const size_t max_session_cache_upstream_hosts_;
  absl::Mutex session_keys_map_mu_;
  absl::flat_hash_map<uint64_t, std::deque<bssl::UniquePtr<SSL_SESSION>>>
      session_keys_map_ ABSL_GUARDED_BY(session_keys_map_mu_);
  bool session_keys_single_use_{false};
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
