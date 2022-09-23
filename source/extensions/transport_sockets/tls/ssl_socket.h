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
#include "source/extensions/transport_sockets/tls/context_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_handshaker.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "absl/container/node_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

#define ALL_SSL_SOCKET_FACTORY_STATS(COUNTER)                                                      \
  COUNTER(ssl_context_update_by_sds)                                                               \
  COUNTER(upstream_context_secrets_not_ready)                                                      \
  COUNTER(downstream_context_secrets_not_ready)

/**
 * Wrapper struct for SSL socket factory stats. @see stats_macros.h
 */
struct SslSocketFactoryStats {
  ALL_SSL_SOCKET_FACTORY_STATS(GENERATE_COUNTER_STRUCT)
};

enum class InitialState { Client, Server };

class SslSocket : public Network::TransportSocket,
                  public Envoy::Ssl::PrivateKeyConnectionCallbacks,
                  public Ssl::HandshakeCallbacks,
                  protected Logger::Loggable<Logger::Id::connection> {
public:
  SslSocket(Envoy::Ssl::ContextSharedPtr ctx, InitialState state,
            const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
            Ssl::HandshakerFactoryCb handshaker_factory_cb);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override { return info_->state() == Ssl::SocketState::HandshakeComplete; }
  void closeSocket(Network::ConnectionEvent close_type) override;
  Network::IoResult doRead(Buffer::Instance& read_buffer) override;
  Network::IoResult doWrite(Buffer::Instance& write_buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}
  // Ssl::PrivateKeyConnectionCallbacks
  void onPrivateKeyMethodComplete() override;
  // Ssl::HandshakeCallbacks
  Network::Connection& connection() const override;
  void onSuccess(SSL* ssl) override;
  void onFailure() override;
  Network::TransportSocketCallbacks* transportSocketCallbacks() override { return callbacks_; }
  void onAsynchronousCertValidationComplete() override;

  SSL* rawSslForTest() const { return rawSsl(); }

protected:
  SSL* rawSsl() const { return info_->ssl(); }

private:
  struct ReadResult {
    uint64_t bytes_read_{0};
    absl::optional<int> error_;
  };
  ReadResult sslReadIntoSlice(Buffer::RawSlice& slice);

  Network::PostIoAction doHandshake();
  void drainErrorQueue();
  void shutdownSsl();
  void shutdownBasic();
  void resumeHandshake();

  const Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  Network::TransportSocketCallbacks* callbacks_{};
  ContextImplSharedPtr ctx_;
  uint64_t bytes_to_retry_{};
  std::string failure_reason_;

  SslHandshakerImplSharedPtr info_;
};

class ClientSslSocketFactory : public Network::CommonUpstreamTransportSocketFactory,
                               public Secret::SecretCallbacks,
                               Logger::Loggable<Logger::Id::config> {
public:
  ClientSslSocketFactory(Envoy::Ssl::ClientContextConfigPtr config,
                         Envoy::Ssl::ContextManager& manager, Stats::Scope& stats_scope);

  ~ClientSslSocketFactory() override;

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr) const override;
  bool implementsSecureTransport() const override;
  absl::string_view defaultServerNameIndication() const override {
    return clientContextConfig()->serverNameIndication();
  }
  bool supportsAlpn() const override { return true; }

  // Secret::SecretCallbacks
  void onAddOrUpdateSecret() override;

  OptRef<const Ssl::ClientContextConfig> clientContextConfig() const override { return {*config_}; }

  Envoy::Ssl::ClientContextSharedPtr sslCtx() override;

private:
  Envoy::Ssl::ContextManager& manager_;
  Stats::Scope& stats_scope_;
  SslSocketFactoryStats stats_;
  Envoy::Ssl::ClientContextConfigPtr config_;
  mutable absl::Mutex ssl_ctx_mu_;
  Envoy::Ssl::ClientContextSharedPtr ssl_ctx_ ABSL_GUARDED_BY(ssl_ctx_mu_);
};

class ServerSslSocketFactory : public Network::DownstreamTransportSocketFactory,
                               public Secret::SecretCallbacks,
                               Logger::Loggable<Logger::Id::config> {
public:
  ServerSslSocketFactory(Envoy::Ssl::ServerContextConfigPtr config,
                         Envoy::Ssl::ContextManager& manager, Stats::Scope& stats_scope,
                         const std::vector<std::string>& server_names);

  ~ServerSslSocketFactory() override;

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
  bool implementsSecureTransport() const override;

  // Secret::SecretCallbacks
  void onAddOrUpdateSecret() override;

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
