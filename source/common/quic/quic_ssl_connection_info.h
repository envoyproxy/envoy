#pragma once

#include <optional>
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
  // Served through the peerCertificate() hook (instead of the SSL object directly) so that it
  // stays answerable from the cache after the SSL object has been released.
  bool peerCertificatePresented() const override { return peerCertificate() != nullptr; }
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  // May return nullptr: with reset-after-handshake enabled the SSL object is released once the
  // peer acknowledges handshake completion. The values served by this class are cached from the
  // crypto stream or from the SSL object before the release.
  SSL* ssl() const override {
    ASSERT(session_.GetCryptoStream() != nullptr);
    return session_.GetCryptoStream()->GetSsl();
  }

  uint16_t ciphersuiteId() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return crypto_stream->CiphersuiteId();
  }

  std::string ciphersuiteString() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return std::string(crypto_stream->CiphersuiteString());
  }

  uint16_t tlsGroupId() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return crypto_stream->TlsGroupId();
  }

  absl::string_view tlsGroupString() const override {
    auto* crypto_stream = session_.GetCryptoStream();
    ASSERT(crypto_stream != nullptr);
    return crypto_stream->TlsGroupString();
  }

  const std::string& tlsVersion() const override {
    static const std::string version("TLSv1.3");
    return version;
  }

  const std::string& alpn() const override {
    if (!alpn_.has_value()) {
      auto* crypto_stream = session_.GetCryptoStream();
      ASSERT(crypto_stream != nullptr);
      alpn_ = std::string(crypto_stream->Alpn());
    }
    return *alpn_;
  }

  const std::string& sni() const override {
    if (!sni_.has_value()) {
      auto* crypto_stream = session_.GetCryptoStream();
      ASSERT(crypto_stream != nullptr);
      sni_ = std::string(crypto_stream->Sni());
    }
    return *sni_;
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

  // Marks the peer certificate as validated and stores the certificate chain built during
  // verification (leaf first, issuers following), which serves the validated-issuer and
  // validated-chain accessors. The certificates are up-ref'd into this object.
  void onCertValidated(const std::vector<bssl::UniquePtr<X509>>& validated_chain = {});

  // Converts and caches the presented peer certificate chain (if any) so that peer certificate
  // queries remain answerable after the SSL object has been released. Must be called while the
  // SSL object is still alive; to be used when reset-after-handshake is enabled, right at
  // handshake completion (the SSL object is released once the peer acknowledges it).
  void cachePeerCertificateChain();

  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  OptRef<const std::vector<bssl::UniquePtr<X509>>> validatedPeerCertChain() const override {
    if (validated_cert_chain_.empty()) {
      return std::nullopt;
    }
    return validated_cert_chain_;
  }

protected:
  // Extensions::TransportSockets::Tls::ConnectionInfoImplBase
  // The QUIC SSL object uses the CRYPTO_BUFFER-based method, so the default X509-based peer
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
  mutable std::optional<std::string> alpn_;
  mutable std::optional<std::string> sni_;
};

} // namespace Quic
} // namespace Envoy
