#include "source/common/tls/client_ssl_socket.h"

#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/hex.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/io_handle_bio.h"
#include "source/common/tls/ssl_handshaker.h"
#include "source/common/tls/utility.h"

#include "absl/strings/str_replace.h"
#include "openssl/err.h"
#include "openssl/x509v3.h"

using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {
SslSocketFactoryStats generateStats(Stats::Scope& store) {
  return {ALL_SSL_SOCKET_FACTORY_STATS(POOL_COUNTER_PREFIX(store, "client_ssl_socket_factory."))};
}
} // namespace

absl::StatusOr<std::unique_ptr<ClientSslSocketFactory>>
ClientSslSocketFactory::create(Envoy::Ssl::ClientContextConfigPtr config,
                               Envoy::Ssl::ContextManager& manager, Stats::Scope& stats_scope) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ClientSslSocketFactory>(
      new ClientSslSocketFactory(std::move(config), manager, stats_scope, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ClientSslSocketFactory::ClientSslSocketFactory(Envoy::Ssl::ClientContextConfigPtr config,
                                               Envoy::Ssl::ContextManager& manager,
                                               Stats::Scope& stats_scope,
                                               absl::Status& creation_status)
    : manager_(manager), stats_scope_(stats_scope), stats_(generateStats(stats_scope)),
      config_(std::move(config)) {
  {
    absl::WriterMutexLock l(&ssl_ctx_mu_);
    auto ctx_or_error = manager_.createSslClientContext(stats_scope_, *config_);
    SET_AND_RETURN_IF_NOT_OK(ctx_or_error.status(), creation_status);
    ssl_ctx_ = *ctx_or_error;
  }
  config_->setSecretUpdateCallback([this]() { return onAddOrUpdateSecret(); });
}

ClientSslSocketFactory::~ClientSslSocketFactory() { manager_.removeContext(ssl_ctx_); }

Network::TransportSocketPtr ClientSslSocketFactory::createTransportSocket(
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
    Upstream::HostDescriptionConstSharedPtr) const {
  // onAddOrUpdateSecret() could be invoked in the middle of checking the existence of ssl_ctx and
  // creating SslSocket using ssl_ctx. Capture ssl_ctx_ into a local variable so that we check and
  // use the same ssl_ctx to create SslSocket.
  Envoy::Ssl::ClientContextSharedPtr ssl_ctx;
  {
    absl::ReaderMutexLock l(&ssl_ctx_mu_);
    ssl_ctx = ssl_ctx_;
  }
  if (ssl_ctx) {
    auto status_or_socket =
        SslSocket::create(std::move(ssl_ctx), InitialState::Client, transport_socket_options,
                          config_->createHandshaker());
    if (status_or_socket.ok()) {
      return std::move(*status_or_socket);
    }
    return std::make_unique<ErrorSslSocket>(status_or_socket.status().message());
  } else {
    ENVOY_LOG(debug, "Create NotReadySslSocket");
    stats_.upstream_context_secrets_not_ready_.inc();
    return std::make_unique<NotReadySslSocket>();
  }
}

bool ClientSslSocketFactory::implementsSecureTransport() const { return true; }

absl::Status ClientSslSocketFactory::onAddOrUpdateSecret() {
  ENVOY_LOG(debug, "Secret is updated.");
  auto ctx_or_error = manager_.createSslClientContext(stats_scope_, *config_);
  RETURN_IF_NOT_OK(ctx_or_error.status());
  {
    absl::WriterMutexLock l(&ssl_ctx_mu_);
    std::swap(*ctx_or_error, ssl_ctx_);
  }
  manager_.removeContext(*ctx_or_error);
  stats_.ssl_context_update_by_sds_.inc();
  return absl::OkStatus();
}

Envoy::Ssl::ClientContextSharedPtr ClientSslSocketFactory::sslCtx() {
  absl::ReaderMutexLock l(&ssl_ctx_mu_);
  return ssl_ctx_;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
