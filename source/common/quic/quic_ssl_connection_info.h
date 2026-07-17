#pragma once

#include <vector>

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

  X509* validatedPeerIssuer() const override;

  // QUIC SSL object doesn't cache local certs after the handshake, and the X509-based local
  // certificate getters are not usable on its CRYPTO_BUFFER-based SSL object.
  // TODO(danzh) cache these fields during cert chain retrieval.
  const std::string& subjectLocalCertificate() const override { return EMPTY_STRING; }
  absl::Span<const std::string> uriSanLocalCertificate() const override { return {}; }
  absl::Span<const std::string> dnsSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> ipSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> emailSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> othernameSansLocalCertificate() const override { return {}; }
  absl::Span<const std::string> oidsLocalCertificate() const override { return {}; }

  void onCertValidated() { cert_validated_ = true; };

  // Stores the certificate chain built during verification (leaf first, issuers following). Used
  // to serve the validated-issuer accessors. The certificates are up-ref'd into this object.
  void setValidatedCertChain(const std::vector<bssl::UniquePtr<X509>>& chain);

protected:
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  // QUIC's SSL object uses the CRYPTO_BUFFER-based method, so the default X509-based peer
  // certificate getters are not usable on it. Convert the CRYPTO_BUFFER peer chain to X509 once
  // and serve the base class accessors from the converted chain.
  bssl::UniquePtr<X509> peerCertificate() const override;
  STACK_OF(X509)* peerCertificateChain() const override;

private:
  quic::QuicSession& session_;
  bool cert_validated_{false};
  // The peer certificate chain converted from the CRYPTO_BUFFER-based SSL object, lazily created
  // and cached. Null if conversion hasn't happened, was queried before the handshake delivered
  // the peer chain, or failed.
  mutable bssl::UniquePtr<STACK_OF(X509)> peer_cert_chain_;
  // The certificate chain built during verification (leaf first, issuers following). Distinct from
  // peer_cert_chain_, which is the unverified list the peer sent. The validated-issuer accessors
  // are served from this so a peer cannot control what is reported as the issuer.
  std::vector<bssl::UniquePtr<X509>> validated_cert_chain_;
};

} // namespace Quic
} // namespace Envoy
