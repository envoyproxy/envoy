#include "source/common/tls/client_context_impl.h"

#include <openssl/ssl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/admin/v3/certs.pb.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/stats/scope.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/utility.h"
#include "source/common/tls/cert_validator/factory.h"
#include "source/common/tls/stats.h"
#include "source/common/tls/utility.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "cert_validator/cert_validator.h"
#include "openssl/evp.h"
#include "openssl/hmac.h"
#include "openssl/pkcs12.h"
#include "openssl/rand.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

absl::StatusOr<std::unique_ptr<ClientContextImpl>>
ClientContextImpl::create(Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
                          Server::Configuration::CommonFactoryContext& factory_context) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ClientContextImpl>(new ClientContextImpl(
      scope, config, config.tlsCertificates(), true, factory_context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ClientContextImpl::ClientContextImpl(
    Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
    const std::vector<std::reference_wrapper<const Ssl::TlsCertificateConfig>>& tls_certificates,
    bool add_selector, Server::Configuration::CommonFactoryContext& factory_context,
    absl::Status& creation_status)
    : ContextImpl(scope, config, tls_certificates, factory_context, nullptr /* additional_init */,
                  creation_status),
      server_name_indication_(config.serverNameIndication()),
      auto_host_sni_(config.autoHostServerNameIndication()),
      allow_renegotiation_(config.allowRenegotiation()),
      max_session_keys_(config.maxSessionKeys()) {
  if (!creation_status.ok()) {
    return;
  }

  // Disallow insecure configuration.
  if (config.autoSniSanMatch() && config.certificateValidationContext() == nullptr) {
    creation_status = absl::InvalidArgumentError(
        "'auto_sni_san_validation' was configured without a validation context");
    return;
  }

  // If a custom TLS certificate selector is used and maxSessionKeys is set to 0
  // then allow multiple certificates.
  //
  // newSSL() installs a cached session before certificate selection callback,
  // and is only keyed by SNI, so it's possible that a session created for
  // cert A can be resumed by a connection that would choose cert B. Therefore
  // to prevent incorrect TLS session resumption, maxSessionKeys should be 0.
  if (!(config.tlsCertificateSelectorFactory() && config.maxSessionKeys() == 0) &&
      tls_contexts_.size() != 1) {
    creation_status =
        absl::InvalidArgumentError("Client TLS context supports only a single certificate");
    return;
  }

  if (tls_contexts_[0].tls_params_.has_value()) {
    ENVOY_LOG(warn, "tls_params on a client TlsCertificate is not supported; "
                    "use context-level tls_params instead");
    tls_contexts_[0].tls_params_.reset();
  }

  if (!parsed_alpn_protocols_.empty()) {
    for (auto& ctx : tls_contexts_) {
      const int rc = SSL_CTX_set_alpn_protos(ctx.ssl_ctx_.get(), parsed_alpn_protocols_.data(),
                                             parsed_alpn_protocols_.size());
      RELEASE_ASSERT(rc == 0, Utility::getLastCryptoError().value_or(""));
    }
  }

  if (max_session_keys_ > 0) {
    SSL_CTX_set_session_cache_mode(tls_contexts_[0].ssl_ctx_.get(), SSL_SESS_CACHE_CLIENT);
    SSL_CTX_sess_set_new_cb(
        tls_contexts_[0].ssl_ctx_.get(), [](SSL* ssl, SSL_SESSION* session) -> int {
          ContextImpl* context_impl =
              static_cast<ContextImpl*>(SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl)));
          ClientContextImpl* client_context_impl = dynamic_cast<ClientContextImpl*>(context_impl);
          RELEASE_ASSERT(client_context_impl != nullptr, ""); // for Coverity
          return client_context_impl->newSessionKey(ssl, session);
        });
  }

  if (add_selector) {
    if (auto factory = config.tlsCertificateSelectorFactory(); factory) {
      tls_certificate_selector_ = factory->createUpstreamTlsCertificateSelector(*this);
      SSL_CTX_set_cert_cb(
          tls_contexts_[0].ssl_ctx_.get(),
          [](SSL* ssl, void*) -> int {
            return static_cast<ClientContextImpl*>(SSL_CTX_get_app_data(SSL_get_SSL_CTX(ssl)))
                ->selectTlsContext(ssl);
          },
          nullptr);
    }
  }
}

absl::StatusOr<bssl::UniquePtr<SSL>>
ClientContextImpl::newSsl(const Network::TransportSocketOptionsConstSharedPtr& options,
                          Upstream::HostDescriptionConstSharedPtr host) {
  absl::StatusOr<bssl::UniquePtr<SSL>> ssl_con_or_status(ContextImpl::newSsl(options, host));
  if (!ssl_con_or_status.ok()) {
    return ssl_con_or_status;
  }

  bssl::UniquePtr<SSL> ssl_con = std::move(ssl_con_or_status.value());

  SessionCacheKey session_cache_key = sessionCacheKey(options, host);

  if (!session_cache_key.sni.empty()) {
    const int rc = SSL_set_tlsext_host_name(ssl_con.get(), session_cache_key.sni.c_str());
    if (rc != 1) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to create upstream TLS due to failure setting SNI: ",
                       Utility::getLastCryptoError().value_or("unknown")));
    }
  }

  // BoringSSL does not expose the callback's original SNI or upstream host when
  // it later returns a new session. Store Envoy's cache key on this SSL object
  // so the callback uses the same identity as the connection.
  auto cache_key = std::make_unique<SessionCacheKey>(std::move(session_cache_key));
  if (SSL_set_ex_data(ssl_con.get(), sslSessionCacheKeyIndex(), cache_key.get()) != 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to create upstream TLS due to failure storing session cache key: ",
                     Utility::getLastCryptoError().value_or("unknown")));
  }
  cache_key.release();

  if (options && !options->verifySubjectAltNameListOverride().empty()) {
    SSL_set_verify(ssl_con.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  }

  // We determine what ALPN using the following precedence:
  // 1. Option-provided ALPN override.
  // 2. ALPN statically configured in the upstream TLS context.
  // 3. Option-provided ALPN fallback.

  // At this point in the code the ALPN has already been set (if present) to the value specified in
  // the TLS context. We've stored this value in parsed_alpn_protocols_ so we can check that to see
  // if it's already been set.
  bool has_alpn_defined = !parsed_alpn_protocols_.empty();
  absl::Status parse_status = absl::OkStatus();
  if (options) {
    // ALPN override takes precedence over TLS context specified, so blindly overwrite it.
    has_alpn_defined |=
        parseAndSetAlpn(options->applicationProtocolListOverride(), *ssl_con, parse_status);
  }

  if (options && !has_alpn_defined && !options->applicationProtocolFallback().empty()) {
    // If ALPN hasn't already been set (either through TLS context or override), use the fallback.
    parseAndSetAlpn(options->applicationProtocolFallback(), *ssl_con, parse_status);
  }
  RETURN_IF_NOT_OK(parse_status);

  if (allow_renegotiation_) {
    SSL_set_renegotiate_mode(ssl_con.get(), ssl_renegotiate_freely);
  }

  if (max_session_keys_ > 0) {
    if (scopeUpstreamTlsSessionCacheBySni()) {
      const auto* key = static_cast<const SessionCacheKey*>(
          SSL_get_ex_data(ssl_con.get(), sslSessionCacheKeyIndex()));
      ASSERT(key != nullptr);
      setSessionForKey(ssl_con.get(), *key);
    } else {
      setSessionFromContextCache(ssl_con.get());
    }
  }

  return ssl_con;
}

int ClientContextImpl::sslSessionCacheKeyIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    // BoringSSL ex-data is per-SSL application storage. Envoy installs the
    // session cache key in newSsl() so the later new-session callback can
    // recover the same key from the SSL*. The ex-data free callback owns and
    // deletes that key when BoringSSL frees the SSL object.
    // See BoringSSL API-CONVENTIONS.md, "ex_data", for this callback-state
    // pattern:
    // https://boringssl.googlesource.com/boringssl/+/HEAD/API-CONVENTIONS.md
    int ssl_session_cache_key_index = SSL_get_ex_new_index(
        0, nullptr, nullptr, nullptr, [](void*, void* ptr, CRYPTO_EX_DATA*, int, long, void*) {
          delete static_cast<SessionCacheKey*>(ptr);
        });
    RELEASE_ASSERT(ssl_session_cache_key_index >= 0, "");
    return ssl_session_cache_key_index;
  }());
}

ClientContextImpl::SessionCacheKey
ClientContextImpl::sessionCacheKey(const Network::TransportSocketOptionsConstSharedPtr& options,
                                   Upstream::HostDescriptionConstSharedPtr host) const {
  SessionCacheKey key;

  // Keep the cache key in lock-step with the SNI selection used for the actual
  // ClientHello. Reusing sessions across these names can resume the wrong TLS
  // identity when multiple upstream logical hosts share a ClientContextImpl.
  if (options && options->serverNameOverride().has_value()) {
    key.sni = options->serverNameOverride().value();
  } else if (auto_host_sni_ && host != nullptr && !host->hostname().empty()) {
    key.sni = host->hostname();
  } else {
    key.sni = server_name_indication_;
  }

  if (host != nullptr) {
    const Network::Address::InstanceConstSharedPtr address = host->address();
    if (address != nullptr && address->ip() != nullptr) {
      // asString() brackets IPv6 addresses and includes the port, which makes
      // the endpoint identity unambiguous for both IP versions.
      key.endpoint = address->asString();
    }
  }

  return key;
}

void ClientContextImpl::setSessionForKey(SSL* ssl, const SessionCacheKey& key) {
  absl::WriterMutexLock lock(session_keys_mu_);
  auto sni_it = session_keys_by_sni_.find(key.sni);
  if (sni_it == session_keys_by_sni_.end() || sni_it->second.sessions.empty()) {
    return;
  }

  auto session_it = sni_it->second.sessions.front();
  if (scopeUpstreamTlsSessionCacheByEndpoint() && !key.endpoint.empty()) {
    auto endpoint_it = sni_it->second.sessions_by_endpoint.find(key.endpoint);
    if (endpoint_it != sni_it->second.sessions_by_endpoint.end() &&
        !endpoint_it->second.sessions.empty()) {
      session_it = endpoint_it->second.sessions.front();
    }
  }

  // Use the newest exact endpoint session when one exists. Otherwise, the
  // newest session for the same SNI preserves reuse for servers that share
  // ticket keys. TLS identity never falls back across SNI values.
  SSL_SESSION* session = session_it->session.get();
  SSL_set_session(ssl, session);

  auto& sni_sessions = sni_it->second.sessions;
  auto sni_session = std::find(sni_sessions.begin(), sni_sessions.end(), session_it);
  ASSERT(sni_session != sni_sessions.end());

  auto endpoint_it = sni_it->second.sessions_by_endpoint.find(session_it->endpoint);
  ASSERT(endpoint_it != sni_it->second.sessions_by_endpoint.end());
  auto& endpoint_sessions = endpoint_it->second.sessions;
  auto endpoint_session = std::find(endpoint_sessions.begin(), endpoint_sessions.end(), session_it);
  ASSERT(endpoint_session != endpoint_sessions.end());

  if (SSL_SESSION_should_be_single_use(session)) {
    sni_sessions.erase(sni_session);
    endpoint_sessions.erase(endpoint_session);
    if (endpoint_sessions.empty()) {
      sni_it->second.sessions_by_endpoint.erase(endpoint_it);
    }
    session_keys_lru_.erase(session_it);
    if (sni_sessions.empty()) {
      session_keys_by_sni_.erase(sni_it);
    }
  } else {
    sni_sessions.erase(sni_session);
    sni_sessions.push_front(session_it);
    endpoint_sessions.erase(endpoint_session);
    endpoint_sessions.push_front(session_it);
    session_keys_lru_.splice(session_keys_lru_.begin(), session_keys_lru_, session_it);
  }
}

void ClientContextImpl::setSessionFromContextCache(SSL* ssl) {
  absl::WriterMutexLock lock(session_keys_mu_);
  if (session_keys_.empty()) {
    return;
  }

  // Runtime-guarded rollback path for the previous context-wide cache
  // behavior. This deliberately ignores SNI and should only be used while the
  // reloadable feature remains available.
  SSL_SESSION* session = session_keys_.front().get();
  SSL_set_session(ssl, session);

  if (SSL_SESSION_should_be_single_use(session)) {
    session_keys_.pop_front();
  }
}

int ClientContextImpl::newSessionKey(SSL* ssl, SSL_SESSION* session) {
  // BoringSSL transfers ownership of |session| to Envoy when this callback
  // returns 1. If Envoy cannot cache it, free it here and still report success.
  if (max_session_keys_ == 0) {
    SSL_SESSION_free(session);
    return 1;
  }

  if (!scopeUpstreamTlsSessionCacheBySni()) {
    absl::WriterMutexLock lock(session_keys_mu_);
    while (session_keys_.size() >= max_session_keys_) {
      session_keys_.pop_back();
    }
    session_keys_.push_front(bssl::UniquePtr<SSL_SESSION>(session));
    return 1; // Tell BoringSSL that we took ownership of the session.
  }

  const auto* key =
      static_cast<const SessionCacheKey*>(SSL_get_ex_data(ssl, sslSessionCacheKeyIndex()));
  if (key == nullptr) {
    SSL_SESSION_free(session);
    return 1;
  }

  absl::WriterMutexLock lock(session_keys_mu_);
  session_keys_lru_.push_front({key->sni, key->endpoint, bssl::UniquePtr<SSL_SESSION>(session)});
  auto sni_it = session_keys_by_sni_.try_emplace(key->sni).first;
  sni_it->second.sessions.push_front(session_keys_lru_.begin());
  auto endpoint_it = sni_it->second.sessions_by_endpoint.try_emplace(key->endpoint).first;
  endpoint_it->second.sessions.push_front(session_keys_lru_.begin());

  // max_session_keys_ retains its existing meaning as the maximum number of
  // cached sessions for this client context. Evict the globally least recently
  // used session, regardless of which SNI produced it.
  while (session_keys_lru_.size() > max_session_keys_) {
    auto evict = session_keys_lru_.end();
    --evict;

    auto evict_sni = session_keys_by_sni_.find(evict->sni);
    ASSERT(evict_sni != session_keys_by_sni_.end());
    auto& sni_sessions = evict_sni->second.sessions;
    auto evict_sni_session = std::find(sni_sessions.begin(), sni_sessions.end(), evict);
    ASSERT(evict_sni_session != sni_sessions.end());
    sni_sessions.erase(evict_sni_session);

    auto evict_endpoint = evict_sni->second.sessions_by_endpoint.find(evict->endpoint);
    ASSERT(evict_endpoint != evict_sni->second.sessions_by_endpoint.end());
    auto& endpoint_sessions = evict_endpoint->second.sessions;
    auto evict_endpoint_session =
        std::find(endpoint_sessions.begin(), endpoint_sessions.end(), evict);
    ASSERT(evict_endpoint_session != endpoint_sessions.end());
    endpoint_sessions.erase(evict_endpoint_session);
    if (endpoint_sessions.empty()) {
      evict_sni->second.sessions_by_endpoint.erase(evict_endpoint);
    }

    if (sni_sessions.empty()) {
      session_keys_by_sni_.erase(evict_sni);
    }
    session_keys_lru_.erase(evict);
  }

  return 1; // Tell BoringSSL that we took ownership of the session.
}

bool ClientContextImpl::scopeUpstreamTlsSessionCacheBySni() const {
  return Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.scope_upstream_tls_session_cache_by_sni");
}

bool ClientContextImpl::scopeUpstreamTlsSessionCacheByEndpoint() const {
  return Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.scope_upstream_tls_session_cache_by_endpoint");
}

// This callback should return 1 on success, 0 on internal error, and negative number
// on failure or pause a handshake.
int ClientContextImpl::selectTlsContext(SSL* ssl) {
  ASSERT(tls_certificate_selector_ != nullptr);

  auto* extended_socket_info = reinterpret_cast<Envoy::Ssl::SslExtendedSocketInfo*>(
      SSL_get_ex_data(ssl, ContextImpl::sslExtendedSocketInfoIndex()));

  auto selection_result = extended_socket_info->certificateSelectionResult();
  switch (selection_result) {
  case Ssl::CertificateSelectionStatus::NotStarted:
    // continue
    break;

  case Ssl::CertificateSelectionStatus::Pending:
    ENVOY_LOG(trace, "already waiting certificate");
    return -1;

  case Ssl::CertificateSelectionStatus::Successful:
    ENVOY_LOG(trace, "wait certificate success");
    return 1;

  default:
    ENVOY_LOG(trace, "wait certificate failed");
    return 0;
  }

  ENVOY_LOG(trace, "upstream TLS context selection result: {}, before selectTlsContext",
            static_cast<int>(selection_result));
  auto transport_socket_options_shared_ptr_ptr =
      static_cast<const Network::TransportSocketOptionsConstSharedPtr*>(SSL_get_app_data(ssl));
  ASSERT(transport_socket_options_shared_ptr_ptr);

  const auto result = tls_certificate_selector_->selectTlsContext(
      *ssl, *transport_socket_options_shared_ptr_ptr,
      extended_socket_info->createCertificateSelectionCallback());

  ENVOY_LOG(trace,
            "upstream TLS context selection result: {}, after selectTlsContext, selection result "
            "status: {}",
            static_cast<int>(extended_socket_info->certificateSelectionResult()),
            static_cast<int>(result.status));
  ASSERT(extended_socket_info->certificateSelectionResult() ==
             Ssl::CertificateSelectionStatus::Pending,
         "invalid selection result");

  extended_socket_info->setCertSelectionHandle(std::move(result.handle));
  switch (result.status) {
  case Ssl::SelectionResult::SelectionStatus::Success:
    extended_socket_info->onCertificateSelectionCompleted(*result.selected_ctx, result.staple,
                                                          false);
    return 1;
  case Ssl::SelectionResult::SelectionStatus::Pending:
    return -1;
  case Ssl::SelectionResult::SelectionStatus::Failed:
    extended_socket_info->onCertificateSelectionCompleted(OptRef<const Ssl::TlsContext>(), false,
                                                          false);
    return 0;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
