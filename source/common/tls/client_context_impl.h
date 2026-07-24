#pragma once

#include <openssl/safestack.h>

#include <array>
#include <deque>
#include <functional>
#include <list>
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

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
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

class ClientContextImpl : public ContextImpl,
                          public Envoy::Ssl::ClientContext,
                          public Ssl::TlsCertificateSelectorContext {
public:
  static absl::StatusOr<std::unique_ptr<ClientContextImpl>>
  create(Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
         Server::Configuration::CommonFactoryContext& factory_context);

  absl::StatusOr<bssl::UniquePtr<SSL>>
  newSsl(const Network::TransportSocketOptionsConstSharedPtr& options,
         Upstream::HostDescriptionConstSharedPtr host) override;

  // Ssl::TlsCertificateSelectorContext
  const std::vector<Ssl::TlsContext>& getTlsContexts() const override { return tls_contexts_; };

  int selectTlsContext(SSL*);

protected:
  ClientContextImpl(
      Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
      const std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>& tls_certificates,
      bool add_selector, Server::Configuration::CommonFactoryContext& factory_context,
      absl::Status& creation_status);

private:
  friend class ClientContextImplPeer;

  struct SniSessionCacheEntry {
    std::string sni;
    bssl::UniquePtr<SSL_SESSION> session;
  };

  using SniSessionCacheList = std::list<SniSessionCacheEntry>;

  struct SniSessionBucket {
    // Iterators into sni_session_keys_lru_, newest first for this SNI.
    std::deque<SniSessionCacheList::iterator> sessions;
  };

  static int sslEffectiveSniIndex();

  int newSessionKey(SSL* ssl, SSL_SESSION* session);
  std::string effectiveSni(const Network::TransportSocketOptionsConstSharedPtr& options,
                           Upstream::HostDescriptionConstSharedPtr host) const;
  void setSessionForSni(SSL* ssl, absl::string_view sni);
  void setSessionFromContextCache(SSL* ssl);
  bool scopeUpstreamTlsSessionCacheBySni() const;

  const std::string server_name_indication_;
  const bool auto_host_sni_;
  const bool allow_renegotiation_;

  const size_t max_session_keys_;
  absl::Mutex session_keys_mu_;
  std::deque<bssl::UniquePtr<SSL_SESSION>> session_keys_ ABSL_GUARDED_BY(session_keys_mu_);
  SniSessionCacheList sni_session_keys_lru_ ABSL_GUARDED_BY(session_keys_mu_);
  absl::flat_hash_map<std::string, SniSessionBucket>
      session_keys_by_sni_ ABSL_GUARDED_BY(session_keys_mu_);
  Ssl::UpstreamTlsCertificateSelectorPtr tls_certificate_selector_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
