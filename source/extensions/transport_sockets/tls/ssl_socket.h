#pragma once

#include <cstdint>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/ssl/private_key/private_key_callbacks.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/transport_sockets/tls/context_impl.h"
#include "extensions/transport_sockets/tls/utility.h"

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
enum class SocketState { PreHandshake, HandshakeInProgress, HandshakeComplete, ShutdownSent };

class SslExtendedSocketInfoImpl : public Envoy::Ssl::SslExtendedSocketInfo {
public:
  void setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus validated) override;
  Envoy::Ssl::ClientValidationStatus certificateValidationStatus() const override;

private:
  Envoy::Ssl::ClientValidationStatus certificate_validation_status_{
      Envoy::Ssl::ClientValidationStatus::NotValidated};
};

class SslSocketInfo : public Envoy::Ssl::ConnectionInfo {
public:
  SslSocketInfo(bssl::UniquePtr<SSL> ssl, ContextImplSharedPtr ctx);

  // Ssl::ConnectionInfo
  bool peerCertificatePresented() const override;
  bool peerCertificateValidated() const override;
  absl::Span<const std::string> uriSanLocalCertificate() const override;
  const std::string& sha256PeerCertificateDigest() const override;
  const std::string& serialNumberPeerCertificate() const override;
  const std::string& issuerPeerCertificate() const override;
  const std::string& subjectPeerCertificate() const override;
  const std::string& subjectLocalCertificate() const override;
  absl::Span<const std::string> uriSanPeerCertificate() const override;
  const std::string& urlEncodedPemEncodedPeerCertificate() const override;
  const std::string& urlEncodedPemEncodedPeerCertificateChain() const override;
  absl::Span<const std::string> dnsSansPeerCertificate() const override;
  absl::Span<const std::string> dnsSansLocalCertificate() const override;
  absl::optional<SystemTime> validFromPeerCertificate() const override;
  absl::optional<SystemTime> expirationPeerCertificate() const override;
  const std::string& sessionId() const override;
  uint16_t ciphersuiteId() const override;
  std::string ciphersuiteString() const override;
  const std::string& tlsVersion() const override;
  absl::optional<std::string> x509Extension(absl::string_view extension_name) const override;
  SSL* ssl() const { return ssl_.get(); }

  bssl::UniquePtr<SSL> ssl_;

private:
  mutable std::vector<std::string> cached_uri_san_local_certificate_;
  mutable std::string cached_sha_256_peer_certificate_digest_;
  mutable std::string cached_serial_number_peer_certificate_;
  mutable std::string cached_issuer_peer_certificate_;
  mutable std::string cached_subject_peer_certificate_;
  mutable std::string cached_subject_local_certificate_;
  mutable std::vector<std::string> cached_uri_san_peer_certificate_;
  mutable std::string cached_url_encoded_pem_encoded_peer_certificate_;
  mutable std::string cached_url_encoded_pem_encoded_peer_cert_chain_;
  mutable std::vector<std::string> cached_dns_san_peer_certificate_;
  mutable std::vector<std::string> cached_dns_san_local_certificate_;
  mutable std::string cached_session_id_;
  mutable std::string cached_tls_version_;
  mutable SslExtendedSocketInfoImpl extended_socket_info_;
};

using SslSocketInfoConstSharedPtr = std::shared_ptr<const SslSocketInfo>;

class SslSocket : public Network::TransportSocket,
                  public Envoy::Ssl::PrivateKeyConnectionCallbacks,
                  protected Logger::Loggable<Logger::Id::connection> {
public:
  SslSocket(Envoy::Ssl::ContextSharedPtr ctx, InitialState state,
            const Network::TransportSocketOptionsSharedPtr& transport_socket_options);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override { return state_ == SocketState::HandshakeComplete; }
  void closeSocket(Network::ConnectionEvent close_type) override;
  Network::IoResult doRead(Buffer::Instance& read_buffer) override;
  Network::IoResult doWrite(Buffer::Instance& write_buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  // Ssl::PrivateKeyConnectionCallbacks
  void onPrivateKeyMethodComplete() override;

  SSL* rawSslForTest() const { return rawSsl(); }

protected:
  SSL* rawSsl() const { return info_->ssl_.get(); }

private:
  struct ReadResult {
    bool commit_slice_{};
    absl::optional<int> error_;
  };
  ReadResult sslReadIntoSlice(Buffer::RawSlice& slice);

  Network::PostIoAction doHandshake();
  void drainErrorQueue();
  void shutdownSsl();
  bool isThreadSafe() const {
    return callbacks_ != nullptr && callbacks_->connection().dispatcher().isThreadSafe();
  }

  const Network::TransportSocketOptionsSharedPtr transport_socket_options_;
  Network::TransportSocketCallbacks* callbacks_{};
  ContextImplSharedPtr ctx_;
  uint64_t bytes_to_retry_{};
  std::string failure_reason_;
  SocketState state_;

  SslSocketInfoConstSharedPtr info_;
};

class ClientSslSocketFactory : public Network::TransportSocketFactory,
                               public Secret::SecretCallbacks,
                               Logger::Loggable<Logger::Id::config> {
public:
  ClientSslSocketFactory(Envoy::Ssl::ClientContextConfigPtr config,
                         Envoy::Ssl::ContextManager& manager, Stats::Scope& stats_scope);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;

  // Secret::SecretCallbacks
  void onAddOrUpdateSecret() override;

private:
  Envoy::Ssl::ContextManager& manager_;
  Stats::Scope& stats_scope_;
  SslSocketFactoryStats stats_;
  Envoy::Ssl::ClientContextConfigPtr config_;
  mutable absl::Mutex ssl_ctx_mu_;
  Envoy::Ssl::ClientContextSharedPtr ssl_ctx_ ABSL_GUARDED_BY(ssl_ctx_mu_);
};

class ServerSslSocketFactory : public Network::TransportSocketFactory,
                               public Secret::SecretCallbacks,
                               Logger::Loggable<Logger::Id::config> {
public:
  ServerSslSocketFactory(Envoy::Ssl::ServerContextConfigPtr config,
                         Envoy::Ssl::ContextManager& manager, Stats::Scope& stats_scope,
                         const std::vector<std::string>& server_names);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
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
