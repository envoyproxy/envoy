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

  // This should be guaranteed during configuration ingestion for client contexts.
  if (tls_contexts_.size() != 1) {
    creation_status =
        absl::InvalidArgumentError("Client TLS context supports only a single certificate");
    return;
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

  const std::string server_name_indication = effectiveSni(options, host);

  if (!server_name_indication.empty()) {
    const int rc = SSL_set_tlsext_host_name(ssl_con.get(), server_name_indication.c_str());
    if (rc != 1) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to create upstream TLS due to failure setting SNI: ",
                       Utility::getLastCryptoError().value_or("unknown")));
    }
  }

  // BoringSSL does not expose the callback's original SNI key when it later
  // returns a new session. Store Envoy's effective SNI on this SSL object so
  // the new-session callback can cache the ticket under the same name that
  // was sent in the ClientHello. An empty string is the valid cache key for
  // connections that do not send SNI.
  auto effective_sni = std::make_unique<std::string>(server_name_indication);
  if (SSL_set_ex_data(ssl_con.get(), sslEffectiveSniIndex(), effective_sni.get()) != 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to create upstream TLS due to failure storing SNI: ",
                     Utility::getLastCryptoError().value_or("unknown")));
  }
  effective_sni.release();

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
      setSessionForSni(ssl_con.get(), server_name_indication);
    } else {
      setSessionFromContextCache(ssl_con.get());
    }
  }

  return ssl_con;
}

int ClientContextImpl::sslEffectiveSniIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    // BoringSSL ex-data is per-SSL application storage. Envoy installs the
    // effective SNI string in newSsl() so the later new-session callback can
    // recover the same cache key from the SSL*. The ex-data free callback owns
    // and deletes that string when BoringSSL frees the SSL object.
    // See BoringSSL API-CONVENTIONS.md, "ex_data", for this callback-state
    // pattern:
    // https://boringssl.googlesource.com/boringssl/+/HEAD/API-CONVENTIONS.md
    int ssl_effective_sni_index = SSL_get_ex_new_index(
        0, nullptr, nullptr, nullptr, [](void*, void* ptr, CRYPTO_EX_DATA*, int, long, void*) {
          delete static_cast<std::string*>(ptr);
        });
    RELEASE_ASSERT(ssl_effective_sni_index >= 0, "");
    return ssl_effective_sni_index;
  }());
}

std::string
ClientContextImpl::effectiveSni(const Network::TransportSocketOptionsConstSharedPtr& options,
                                Upstream::HostDescriptionConstSharedPtr host) const {
  // Keep the cache key in lock-step with the SNI selection used for the actual
  // ClientHello. Reusing sessions across these names can resume the wrong TLS
  // identity when multiple upstream logical hosts share a ClientContextImpl.
  if (options && options->serverNameOverride().has_value()) {
    return options->serverNameOverride().value();
  }
  if (auto_host_sni_ && host != nullptr && !host->hostname().empty()) {
    return host->hostname();
  }
  return server_name_indication_;
}

void ClientContextImpl::setSessionForSni(SSL* ssl, absl::string_view sni) {
  absl::WriterMutexLock lock(session_keys_mu_);
  auto it = session_keys_by_sni_.find(sni);
  if (it == session_keys_by_sni_.end() || it->second.sessions.empty()) {
    return;
  }

  // Use the newest SSL_SESSION for this SNI. In TLS 1.3, BoringSSL represents
  // resumption tickets as SSL_SESSION objects, and those tickets can be
  // single-use, so remove them immediately after installing them on the new SSL
  // object.
  const auto session_it = it->second.sessions.front();
  SSL_SESSION* session = session_it->session.get();
  SSL_set_session(ssl, session);

  if (SSL_SESSION_should_be_single_use(session)) {
    it->second.sessions.pop_front();
    sni_session_keys_lru_.erase(session_it);
    if (it->second.sessions.empty()) {
      session_keys_by_sni_.erase(it);
    }
  } else {
    sni_session_keys_lru_.splice(sni_session_keys_lru_.begin(), sni_session_keys_lru_, session_it);
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

  const auto* effective_sni =
      static_cast<const std::string*>(SSL_get_ex_data(ssl, sslEffectiveSniIndex()));
  if (effective_sni == nullptr) {
    SSL_SESSION_free(session);
    return 1;
  }

  absl::WriterMutexLock lock(session_keys_mu_);
  const std::string& sni = *effective_sni;
  sni_session_keys_lru_.push_front({sni, bssl::UniquePtr<SSL_SESSION>(session)});
  auto it = session_keys_by_sni_.try_emplace(sni).first;
  it->second.sessions.push_front(sni_session_keys_lru_.begin());

  // max_session_keys_ retains its existing meaning as the maximum number of
  // cached sessions for this client context. Evict the globally least recently
  // used session, regardless of which SNI produced it.
  while (sni_session_keys_lru_.size() > max_session_keys_) {
    auto evict = sni_session_keys_lru_.end();
    --evict;
    auto bucket = session_keys_by_sni_.find(evict->sni);
    ASSERT(bucket != session_keys_by_sni_.end());
    ASSERT(!bucket->second.sessions.empty());
    ASSERT(bucket->second.sessions.back() == evict);
    bucket->second.sessions.pop_back();
    if (bucket->second.sessions.empty()) {
      session_keys_by_sni_.erase(bucket);
    }
    sni_session_keys_lru_.erase(evict);
  }

  return 1; // Tell BoringSSL that we took ownership of the session.
}

bool ClientContextImpl::scopeUpstreamTlsSessionCacheBySni() const {
  return Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.scope_upstream_tls_session_cache_by_sni");
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
