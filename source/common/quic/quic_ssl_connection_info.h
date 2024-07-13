#pragma once

#include "source/common/common/empty_string.h"
#include "source/common/tls/connection_info_impl_base.h"

#include "quiche/quic/core/quic_session.h"

namespace Envoy {
namespace Quic {

// A wrapper of a QUIC session to be passed around as an indicator of ssl support and to provide
// access to the SSL object in QUIC crypto stream.
class QuicSslConnectionInfo : public Extensions::TransportSockets::Tls::ConnectionInfoImplBase {
public:
  QuicSslConnectionInfo(quic::QuicSession& session) : session_(session) {}

  // Ssl::ConnectionInfo
  bool peerCertificateValidated() const override { return cert_validated_; };
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  SSL* ssl() const override {
    ASSERT(session_.GetCryptoStream() != nullptr);
    ASSERT(session_.GetCryptoStream()->GetSsl() != nullptr);
    return session_.GetCryptoStream()->GetSsl();
  }

  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  // TODO(#23809) populate those field once we support mutual TLS.
  bool peerCertificatePresented() const override { return false; }
  const std::string& sha256PeerCertificateDigest() const override { return EMPTY_STRING; }
  const std::string& sha1PeerCertificateDigest() const override { return EMPTY_STRING; }
  absl::Span<const std::string> uriSanPeerCertificate() const override { return {}; }
  const std::string& serialNumberPeerCertificate() const override { return EMPTY_STRING; }
  const std::string& issuerPeerCertificate() const override { return EMPTY_STRING; }
  const std::string& subjectPeerCertificate() const override { return EMPTY_STRING; }
  const std::string& urlEncodedPemEncodedPeerCertificate() const override { return EMPTY_STRING; }
  const std::string& urlEncodedPemEncodedPeerCertificateChain() const override {
    return EMPTY_STRING;
  }
  absl::Span<const std::string> dnsSansPeerCertificate() const override { return {}; }
  absl::optional<SystemTime> validFromPeerCertificate() const override { return absl::nullopt; }
  absl::optional<SystemTime> expirationPeerCertificate() const override { return absl::nullopt; }
  // QUIC SSL object doesn't cache local certs after the handshake.
  // TODO(danzh) cache these fields during cert chain retrieval.
  const std::string& subjectLocalCertificate() const override { return EMPTY_STRING; }
  absl::Span<const std::string> uriSanLocalCertificate() const override { return {}; }
  absl::Span<const std::string> dnsSansLocalCertificate() const override { return {}; }

  void onCertValidated() { cert_validated_ = true; };

private:
  quic::QuicSession& session_;
  bool cert_validated_{false};
};

} // namespace Quic
} // namespace Envoy
