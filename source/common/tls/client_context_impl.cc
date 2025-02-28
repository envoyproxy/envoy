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
  auto ret = std::unique_ptr<ClientContextImpl>(
      new ClientContextImpl(scope, config, factory_context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ClientContextImpl::ClientContextImpl(Stats::Scope& scope,
                                     const Envoy::Ssl::ClientContextConfig& config,
                                     Server::Configuration::CommonFactoryContext& factory_context,
                                     absl::Status& creation_status)
    : ContextImpl(scope, config, factory_context, nullptr /* additional_init */, creation_status),
      server_name_indication_(config.serverNameIndication()),
      auto_host_sni_(config.autoHostServerNameIndication()),
      allow_renegotiation_(config.allowRenegotiation()),
      enforce_rsa_key_usage_(config.enforceRsaKeyUsage()),
      max_session_keys_(config.maxSessionKeys()),
      per_host_session_cache_config_(config.perHostSessionCacheConfig()) {
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
  ASSERT(tls_contexts_.size() == 1);
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
          auto* host = static_cast<const Upstream::HostDescriptionConstSharedPtr*>(
              SSL_get_ex_data(ssl, sslSocketUpstreamHostIndex()));
          RELEASE_ASSERT(host != nullptr, "upstream host description not set in SSL socket");
          return client_context_impl->newSessionKey(session, *host);
        });
  }
}

int ClientContextImpl::sslSocketUpstreamHostIndex() {
  CONSTRUCT_ON_FIRST_USE(int, []() -> int {
    int ssl_socket_upstream_host_index =
        SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    RELEASE_ASSERT(ssl_socket_upstream_host_index >= 0, "");
    return ssl_socket_upstream_host_index;
  }());
}

absl::StatusOr<bssl::UniquePtr<SSL>>
ClientContextImpl::newSsl(const Network::TransportSocketOptionsConstSharedPtr& options,
                          const Upstream::HostDescriptionConstSharedPtr& host) {
  absl::StatusOr<bssl::UniquePtr<SSL>> ssl_con_or_status(ContextImpl::newSsl(options, host));
  if (!ssl_con_or_status.ok()) {
    return ssl_con_or_status;
  }

  bssl::UniquePtr<SSL> ssl_con = std::move(ssl_con_or_status.value());
  SSL_set_ex_data(ssl_con.get(), sslSocketUpstreamHostIndex(),
                  const_cast<Upstream::HostDescriptionConstSharedPtr*>(&host));

  std::string server_name_indication;
  if (options && options->serverNameOverride().has_value()) {
    server_name_indication = options->serverNameOverride().value();
  } else if (auto_host_sni_ && host != nullptr && !host->hostname().empty()) {
    server_name_indication = host->hostname();
  } else {
    server_name_indication = server_name_indication_;
  }

  if (!server_name_indication.empty()) {
    const int rc = SSL_set_tlsext_host_name(ssl_con.get(), server_name_indication.c_str());
    if (rc != 1) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to create upstream TLS due to failure setting SNI: ",
                       Utility::getLastCryptoError().value_or("unknown")));
    }
  }

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

  SSL_set_enforce_rsa_key_usage(ssl_con.get(), enforce_rsa_key_usage_);

  if (max_session_keys_ > 0 || per_host_session_cache_config_.has_value()) {
    uint64_t key = sessionCacheKey(host);
    if (session_keys_single_use_) {
      // Stored single-use session keys, use write/write locks.
      absl::WriterMutexLock l(&session_keys_map_mu_);
      auto it = session_keys_map_.find(key);
      if (it != session_keys_map_.end() && !it->second.empty()) {
        auto& session_keys = it->second;
        // Use the most recently stored session key, since it has the highest
        // probability of still being recognized/accepted by the server.
        SSL_SESSION* session = session_keys.front().get();
        SSL_set_session(ssl_con.get(), session);
        // Remove single-use session key (TLS 1.3) after first use.
        if (SSL_SESSION_should_be_single_use(session)) {
          session_keys.pop_front();
        }
      }
    } else {
      // Never stored single-use session keys, use read/write locks.
      absl::ReaderMutexLock l(&session_keys_map_mu_);
      auto it = session_keys_map_.find(key);
      if (it != session_keys_map_.end() && !it->second.empty()) {
        // Use the most recently stored session key, since it has the highest
        // probability of still being recognized/accepted by the server.
        SSL_SESSION* session = it->second.front().get();
        SSL_set_session(ssl_con.get(), session);
      }
    }
  }

  return ssl_con;
}

int ClientContextImpl::newSessionKey(SSL_SESSION* session,
                                     const Upstream::HostDescriptionConstSharedPtr& host) {
  // In case we ever store single-use session key (TLS 1.3),
  // we need to switch to using write/write locks.
  if (SSL_SESSION_should_be_single_use(session)) {
    session_keys_single_use_ = true;
  }

  auto max_session_keys = max_session_keys_;
  uint64_t key = sessionCacheKey(host);
  absl::WriterMutexLock l(&session_keys_map_mu_);

  auto it = session_keys_map_.find(key);
  if (it == session_keys_map_.end()) {
    std::tie(it, std::ignore) =
        session_keys_map_.emplace(key, std::deque<bssl::UniquePtr<SSL_SESSION>>());
    if (per_host_session_cache_config_.has_value()) {
      max_session_keys = per_host_session_cache_config_->max_session_keys_per_host_;

      if (session_keys_map_.size() > per_host_session_cache_config_->max_hosts_) {
        // When the cache size has exceeded, remove the key next to the one that was just added
        // for randomization.
        auto next_it = std::next(it);
        if (next_it == session_keys_map_.end()) {
          // Wrap around to the first key.
          next_it = session_keys_map_.begin();
        }
        session_keys_map_.erase(next_it);
      }
    } else {
      // If per host upstream cache is disabled, we fallback to the original behavior by
      // storing session keys under the first key (0).
      ASSERT(key == 0);
    }
  }

  auto& session_keys = it->second;
  // Evict oldest entries.
  while (session_keys.size() >= max_session_keys) {
    session_keys.pop_back();
  }
  // Add new session key at the front of the queue, so that it's used first.
  session_keys.push_front(bssl::UniquePtr<SSL_SESSION>(session));
  return 1; // Tell BoringSSL that we took ownership of the session.
}

uint64_t ClientContextImpl::sessionCacheKey(const Upstream::HostDescriptionConstSharedPtr& host) {
  // If per host upstream cache is disabled, we fallback to the original behavior by
  // storing session keys under the first key (0).
  if (!per_host_session_cache_config_.has_value() || host == nullptr) {
    return 0;
  }

  const auto& addr_str = host->address()->asString();
  if (addr_str.empty()) {
    return 0;
  }
  return HashUtil::xxHash64(addr_str);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
