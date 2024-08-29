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
#include "source/common/tls/utility.h"

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
  static absl::StatusOr<std::unique_ptr<SslSocket>>
  create(Envoy::Ssl::ContextSharedPtr ctx, InitialState state,
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
  void onAsynchronousCertificateSelectionComplete() override;

  SSL* rawSslForTest() const { return rawSsl(); }

protected:
  SSL* rawSsl() const { return info_->ssl(); }

private:
  SslSocket(Envoy::Ssl::ContextSharedPtr ctx,
            const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options);
  absl::Status initialize(InitialState state, Ssl::HandshakerFactoryCb handshaker_factory_cb);

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

class InvalidSslSocket : public Network::TransportSocket {
public:
  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks&) override {}
  std::string protocol() const override { return EMPTY_STRING; }
  bool canFlushClose() override { return true; }
  void closeSocket(Network::ConnectionEvent) override {}
  Network::IoResult doRead(Buffer::Instance&) override {
    return {Network::PostIoAction::Close, 0, false};
  }
  Network::IoResult doWrite(Buffer::Instance&, bool) override {
    return {Network::PostIoAction::Close, 0, false};
  }
  void onConnected() override {}
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}
};

// This SslSocket will be used when SSL secret is not fetched from SDS server.
class NotReadySslSocket : public InvalidSslSocket {
public:
  // Network::TransportSocket
  absl::string_view failureReason() const override;
};

class ErrorSslSocket : public InvalidSslSocket {
public:
  ErrorSslSocket(absl::string_view error) : error_(error) {}

  // Network::TransportSocket
  absl::string_view failureReason() const override { return error_; }

private:
  std::string error_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
