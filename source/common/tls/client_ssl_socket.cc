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

ClientSslSocketFactory::ClientSslSocketFactory(Envoy::Ssl::ClientContextConfigPtr config,
                                               Envoy::Ssl::ContextManager& manager,
                                               Stats::Scope& stats_scope)
    : manager_(manager), stats_scope_(stats_scope), stats_(generateStats(stats_scope)),
      config_(std::move(config)),
      ssl_ctx_(manager_.createSslClientContext(stats_scope_, *config_)) {
  config_->setSecretUpdateCallback([this]() { onAddOrUpdateSecret(); });
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
      return std::move(status_or_socket.value());
    }
    return std::make_unique<ErrorSslSocket>(status_or_socket.status().message());
  } else {
    ENVOY_LOG(debug, "Create NotReadySslSocket");
    stats_.upstream_context_secrets_not_ready_.inc();
    return std::make_unique<NotReadySslSocket>();
  }
}

bool ClientSslSocketFactory::implementsSecureTransport() const { return true; }

void ClientSslSocketFactory::onAddOrUpdateSecret() {
  ENVOY_LOG(debug, "Secret is updated.");
  auto ctx = manager_.createSslClientContext(stats_scope_, *config_);
  {
    absl::WriterMutexLock l(&ssl_ctx_mu_);
    std::swap(ctx, ssl_ctx_);
  }
  manager_.removeContext(ctx);
  stats_.ssl_context_update_by_sds_.inc();
}

Envoy::Ssl::ClientContextSharedPtr ClientSslSocketFactory::sslCtx() {
  absl::ReaderMutexLock l(&ssl_ctx_mu_);
  return ssl_ctx_;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
