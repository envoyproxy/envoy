#include "source/common/quic/quic_ssl_connection_info.h"

#include "openssl/ssl.h"
#include "openssl/x509.h"

namespace Envoy {
namespace Quic {

bssl::UniquePtr<X509> QuicSslConnectionInfo::peerCertificate() const {
  STACK_OF(X509)* chain = peerCertificateChain();
  if (chain == nullptr || sk_X509_num(chain) == 0) {
    return nullptr;
  }
  return bssl::UpRef(sk_X509_value(chain, 0));
}

STACK_OF(X509)* QuicSslConnectionInfo::peerCertificateChain() const {
  if (peer_cert_chain_ != nullptr) {
    return peer_cert_chain_.get();
  }
  // The chain may legitimately not be available yet if queried before the handshake delivered the
  // peer certificates; in that case the conversion is retried on the next query.
  const STACK_OF(CRYPTO_BUFFER)* certs = SSL_get0_peer_certificates(ssl());
  if (certs == nullptr || sk_CRYPTO_BUFFER_num(certs) == 0) {
    return nullptr;
  }
  bssl::UniquePtr<STACK_OF(X509)> chain(sk_X509_new_null());
  for (size_t i = 0; i < sk_CRYPTO_BUFFER_num(certs); i++) {
    const CRYPTO_BUFFER* buffer = sk_CRYPTO_BUFFER_value(certs, i);
    const uint8_t* data = CRYPTO_BUFFER_data(buffer);
    bssl::UniquePtr<X509> cert(d2i_X509(nullptr, &data, CRYPTO_BUFFER_len(buffer)));
    if (cert == nullptr || !bssl::PushToStack(chain.get(), std::move(cert))) {
      // A certificate the TLS stack accepted should always be parseable; treat a malformed chain
      // as not presented rather than exposing a partial chain.
      return nullptr;
    }
  }
  peer_cert_chain_ = std::move(chain);
  return peer_cert_chain_.get();
}

X509* QuicSslConnectionInfo::validatedPeerIssuer() const {
  // The whole presented chain is validated for QUIC connections, so the leaf's direct issuer is
  // the second element of the chain when present.
  STACK_OF(X509)* chain = peerCertificateChain();
  if (chain == nullptr || sk_X509_num(chain) < 2) {
    return nullptr;
  }
  return sk_X509_value(chain, 1);
}

} // namespace Quic
} // namespace Envoy
