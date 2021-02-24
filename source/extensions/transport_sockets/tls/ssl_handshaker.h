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

#include "common/common/logger.h"

#include "extensions/transport_sockets/tls/utility.h"

#include "absl/container/node_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class SslExtendedSocketInfoImpl : public Envoy::Ssl::SslExtendedSocketInfo {
public:
  void setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus validated) override;
  Envoy::Ssl::ClientValidationStatus certificateValidationStatus() const override;

private:
  Envoy::Ssl::ClientValidationStatus certificate_validation_status_{
      Envoy::Ssl::ClientValidationStatus::NotValidated};
};

class SslHandshakerImpl : public Ssl::ConnectionInfo,
                          public Ssl::Handshaker,
                          protected Logger::Loggable<Logger::Id::connection> {
public:
  SslHandshakerImpl(bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
                    Ssl::HandshakeCallbacks* handshake_callbacks);

  // Ssl::ConnectionInfo
  bool peerCertificatePresented() const override;
  bool peerCertificateValidated() const override;
  absl::Span<const std::string> uriSanLocalCertificate() const override;
  const std::string& sha256PeerCertificateDigest() const override;
  const std::string& sha1PeerCertificateDigest() const override;
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

  // Ssl::Handshaker
  Network::PostIoAction doHandshake() override;

  Ssl::SocketState state() const { return state_; }
  void setState(Ssl::SocketState state) { state_ = state; }
  SSL* ssl() const { return ssl_.get(); }
  Ssl::HandshakeCallbacks* handshakeCallbacks() { return handshake_callbacks_; }

  bssl::UniquePtr<SSL> ssl_;

private:
  Ssl::HandshakeCallbacks* handshake_callbacks_;

  Ssl::SocketState state_;
  mutable std::vector<std::string> cached_uri_san_local_certificate_;
  mutable std::string cached_sha_256_peer_certificate_digest_;
  mutable std::string cached_sha_1_peer_certificate_digest_;
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

using SslHandshakerImplSharedPtr = std::shared_ptr<SslHandshakerImpl>;

class HandshakerFactoryContextImpl : public Ssl::HandshakerFactoryContext {
public:
  HandshakerFactoryContextImpl(Api::Api& api, absl::string_view alpn_protocols)
      : api_(api), alpn_protocols_(alpn_protocols) {}

  // HandshakerFactoryContext
  Api::Api& api() override { return api_; }
  absl::string_view alpnProtocols() const override { return alpn_protocols_; }

private:
  Api::Api& api_;
  const std::string alpn_protocols_;
};

class HandshakerFactoryImpl : public Ssl::HandshakerFactory {
public:
  std::string name() const override { return "envoy.default_tls_handshaker"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  Ssl::HandshakerFactoryCb createHandshakerCb(const Protobuf::Message&,
                                              Ssl::HandshakerFactoryContext&,
                                              ProtobufMessage::ValidationVisitor&) override {
    // The default HandshakerImpl doesn't take a config or use the HandshakerFactoryContext.
    return [](bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
              Ssl::HandshakeCallbacks* handshake_callbacks) {
      return std::make_shared<SslHandshakerImpl>(std::move(ssl), ssl_extended_socket_info_index,
                                                 handshake_callbacks);
    };
  }

  Ssl::HandshakerCapabilities capabilities() const override {
    // The default handshaker impl requires Envoy to handle all enumerated behaviors.
    return Ssl::HandshakerCapabilities{};
  }

  Ssl::SslCtxCb sslctx_cb(Ssl::HandshakerFactoryContext&) const override {
    // The default handshaker impl doesn't additionally modify SSL_CTX.
    return [](SSL_CTX*) {};
  }

  static HandshakerFactory* getDefaultHandshakerFactory() {
    static HandshakerFactoryImpl default_handshaker_factory;
    return &default_handshaker_factory;
  }
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
